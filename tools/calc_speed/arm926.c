/* arm926.c: a QEMU TCG plugin that estimates how many cycles an ARM program
 * would take on the TI-Nspire CX II's ARM926EJ-S. tests/calc_speed.sh runs
 * PocketSNES's game harness (tests/asm/game_test, built like the calculator
 * build) under qemu-arm with this plugin; see TESTING.md.
 *
 * The model:
 *  - Pipeline: 1 cycle per instruction, plus the ARM926EJ-S's extra cycles
 *    for taken branches and other writes to the PC, LDM/STM (a cycle per
 *    register), multiplies, register-specified shifts, and a load's result
 *    used by the next instruction (or the one after, for bytes and halves).
 *  - Caches: 16 KB instruction and 8 KB data cache (what the CX II's cache
 *    type register reports), 4-way, 32-byte lines, round-robin replacement;
 *    the data cache is write-back and allocates on reads only.
 *  - Memory: one bus. A cache miss waits for the write buffer to empty, then
 *    takes "fill" cycles; a store that misses the cache goes into the write
 *    buffer (4 entries; the words of one STM share an entry), which writes
 *    one entry per "write" cycles (plus "word" per extra word) in the
 *    background and stalls the CPU only when full; a dirty line thrown out
 *    of the cache keeps the bus busy for "writeback" cycles.
 * fill, write, word and writeback are the plugin's arguments (in CPU cycles);
 * tests/calc_speed.sh passes values fitted to speed tests run on the
 * calculator.
 *
 * Arguments (-plugin arm926.so,arg=value,...):
 *   out=FILE     results (default arm926.out)
 *   mark=ADDR    address (hex) of a function called at the start of every
 *                frame: only cycles after its first call count, and the
 *                cycles between calls are listed per frame
 *   fill=N write=N word=N writeback=N    memory timings (cycles)
 * The output lists the totals, the model's cycles and the instructions of
 * every frame, and the instructions and cycles of every instruction address
 * (for per-function sums by tests/calc_speed.sh). */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t p_fill = 26, p_write = 10, p_word = 2, p_writeback = 20;
static uint64_t mark_addr;
static const char *out_path = "arm926.out";

/* What the timing needs to know about an instruction */
struct insn
{
    uint64_t pc;
    uint32_t reads;	/* registers the instruction reads (bit n = rn) */
    uint8_t base;	/* cycles when it runs and nothing stalls */
    uint8_t taken;	/* extra cycles if the next instruction isn't pc + 4 */
    uint8_t load_rd;	/* register a load writes (0xff: none) */
    uint8_t load_lat;	/* cycles its result isn't ready for the next instruction */
    uint8_t thumb;
    uint64_t count, cycles;
};

/* State, all on one vCPU */
static uint64_t now;			/* the cycle counter */
static uint64_t bus_free;		/* the memory bus is busy until then */
static uint64_t wb_done[4];		/* when each write buffer entry is written */
static int wb_count;			/* entries in the buffer (wb_done[0] oldest) */
static uint64_t wb_last_addr;		/* the newest entry's last word, for STM bursts */
static int wb_last_words;
static struct insn *wb_last_insn;
static struct insn *prev;		/* the instruction before this one */
static int prev_loaded;			/* it was a load and it ran (its condition passed) */
static uint8_t pending_rd = 0xff;	/* a byte/half load two instructions back */
static int counting;			/* after the first frame mark */
static GArray *frame_cycles;		/* cycles at each frame mark */
static GArray *frame_insns;		/* instructions run at each frame mark */
static uint64_t stat_insns, stat_imiss, stat_dmiss, stat_smiss, stat_wbstall, stat_fill_wait;
static uint64_t stat_cycles_start;
static GPtrArray *all_insns;

/* Caches: tags per set and way, round-robin victim per set */
#define LINE_SHIFT 5
struct cache
{
    int sets, ways;
    uint64_t *tag;	/* sets * ways; ~0 = empty */
    uint8_t *dirty;
    uint8_t *victim;
};

static struct cache icache, dcache;

static void cache_init(struct cache *c, int bytes, int ways)
{
    c->ways = ways;
    c->sets = (bytes >> LINE_SHIFT) / ways;
    c->tag = g_new(uint64_t, c->sets * ways);
    c->dirty = g_new0(uint8_t, c->sets * ways);
    c->victim = g_new0(uint8_t, c->sets);
    for (int i = 0; i < c->sets * ways; i++)
	c->tag[i] = ~(uint64_t) 0;
}

/* Returns the way holding the line, or -1 */
static int cache_find(struct cache *c, uint64_t addr, int *set_out)
{
    uint64_t line = addr >> LINE_SHIFT;
    int set = line % c->sets;
    *set_out = set;
    for (int w = 0; w < c->ways; w++)
	if (c->tag[set * c->ways + w] == line)
	    return w;
    return -1;
}

/* Puts the line in; returns 1 if a dirty line was thrown out */
static int cache_fill(struct cache *c, uint64_t addr, int set)
{
    int w = c->victim[set];
    int was_dirty = c->dirty[set * c->ways + w] && c->tag[set * c->ways + w] != ~(uint64_t) 0;
    c->victim[set] = (w + 1) % c->ways;
    c->tag[set * c->ways + w] = addr >> LINE_SHIFT;
    c->dirty[set * c->ways + w] = 0;
    return was_dirty;
}

static void stall_to(uint64_t t, struct insn *in)
{
    if (t > now)
    {
	in->cycles += t - now;
	now = t;
    }
}

static void wb_retire(void)
{
    int k = 0;
    while (k < wb_count && wb_done[k] <= now)
	k++;
    if (k)
    {
	memmove(wb_done, wb_done + k, (wb_count - k) * sizeof wb_done[0]);
	wb_count -= k;
    }
}

/* A line fill (instruction or data read miss): the write buffer is emptied
 * first, then the line comes in. */
static void line_fill(struct insn *in)
{
    uint64_t start = now;
    if (wb_count && wb_done[wb_count - 1] > start)
	start = wb_done[wb_count - 1];
    if (bus_free > start)
	start = bus_free;
    stat_fill_wait += start - now;
    stall_to(start + p_fill, in);
    bus_free = now;
    wb_count = 0;
}

static void store_miss(struct insn *in, uint64_t addr)
{
    wb_retire();
    /* The next word of the same STM joins the newest entry */
    if (wb_count && wb_last_insn == in && addr == wb_last_addr + 4 && wb_last_words < 8 &&
	wb_done[wb_count - 1] > now)
    {
	wb_done[wb_count - 1] += p_word;
	if (bus_free < wb_done[wb_count - 1])
	    bus_free = wb_done[wb_count - 1];
	wb_last_addr = addr;
	wb_last_words++;
	return;
    }
    if (wb_count == 4)
    {
	uint64_t before = now;
	stall_to(wb_done[0], in);
	stat_wbstall += now - before;
	wb_retire();
    }
    uint64_t start = now;
    if (wb_count && wb_done[wb_count - 1] > start)
	start = wb_done[wb_count - 1];
    if (bus_free > start)
	start = bus_free;
    wb_done[wb_count++] = start + p_write;
    bus_free = start + p_write;
    wb_last_insn = in;
    wb_last_addr = addr;
    wb_last_words = 1;
}

static void vcpu_mem(unsigned int vcpu, qemu_plugin_meminfo_t info, uint64_t vaddr, void *udata)
{
    struct insn *in = udata;
    int set, way;
    if (!counting)
	return;
    way = cache_find(&dcache, vaddr, &set);
    if (qemu_plugin_mem_is_store(info))
    {
	if (way >= 0)
	    dcache.dirty[set * dcache.ways + way] = 1;
	else
	{
	    stat_smiss++;
	    store_miss(in, vaddr);
	}
    }
    else
    {
	prev_loaded = 1;
	if (way < 0)
	{
	    stat_dmiss++;
	    line_fill(in);
	    if (cache_fill(&dcache, vaddr, set))
		bus_free += p_writeback;
	}
    }
}

static void vcpu_insn(unsigned int vcpu, void *udata)
{
    struct insn *in = udata;

    if (in->pc == mark_addr)
    {
	if (!counting)
	{
	    counting = 1;
	    stat_cycles_start = now;
	}
	g_array_append_val(frame_cycles, now);
	g_array_append_val(frame_insns, stat_insns);
    }
    if (!counting)
	return;

    if (prev)
    {
	/* A taken branch or other jump: the pipeline refills */
	if (prev->taken && in->pc != prev->pc + (prev->thumb ? 2 : 4))
	{
	    prev->cycles += prev->taken;
	    now += prev->taken;
	}
	/* A load's result needed right away */
	if (prev_loaded && prev->load_rd != 0xff && (in->reads >> prev->load_rd & 1))
	{
	    in->cycles += prev->load_lat;
	    now += prev->load_lat;
	}
	else if (pending_rd != 0xff && (in->reads >> pending_rd & 1))
	{
	    in->cycles += 1;
	    now += 1;
	}
	pending_rd = prev_loaded && prev->load_lat > 1 ? prev->load_rd : 0xff;
    }

    /* Instruction fetch */
    {
	int set;
	if (cache_find(&icache, in->pc, &set) < 0)
	{
	    stat_imiss++;
	    line_fill(in);
	    cache_fill(&icache, in->pc, set);
	}
    }

    now += in->base;
    in->cycles += in->base;
    in->count++;
    stat_insns++;
    prev = in;
    prev_loaded = 0;
}

static int popcount16(uint32_t x)
{
    return __builtin_popcount(x & 0xffff);
}

/* Decodes an ARM (32-bit) instruction for the timing */
static void decode_arm(struct insn *in, uint32_t w)
{
    uint32_t cond = w >> 28;
    uint32_t rn = (w >> 16) & 15, rd = (w >> 12) & 15, rs = (w >> 8) & 15, rm = w & 15;
    in->base = 1;
    in->taken = 0;
    in->load_rd = 0xff;
    in->load_lat = 1;
    in->reads = 0;

    if (cond == 0xf)
    {
	if (((w >> 25) & 7) == 5)		/* BLX immediate */
	    in->taken = 2;
	return;				/* PLD and the like */
    }
    switch ((w >> 25) & 7)
    {
    case 0:
	if ((w & 0x0ffffff0) == 0x012fff10 || (w & 0x0ffffff0) == 0x012fff30)
	{
	    /* BX, BLX register */
	    in->reads = 1u << rm;
	    in->taken = 2;
	    return;
	}
	if ((w & 0x0fc000f0) == 0x00000090)
	{
	    /* MUL, MLA */
	    in->reads = (1u << rm) | (1u << rs) | ((w & (1 << 21)) ? 1u << rd : 0);
	    in->base = (w & (1 << 21)) ? 3 : 2;
	    return;
	}
	if ((w & 0x0f8000f0) == 0x00800090)
	{
	    /* UMULL, UMLAL, SMULL, SMLAL */
	    in->reads = (1u << rm) | (1u << rs);
	    in->base = (w & (1 << 21)) ? 4 : 3;
	    return;
	}
	if ((w & 0x0fb00ff0) == 0x01000090)
	{
	    /* SWP, SWPB */
	    in->reads = (1u << rn) | (1u << rm);
	    in->base = 2;
	    in->load_rd = rd;
	    return;
	}
	if ((w & 0x0e000090) == 0x00000090 && (w & 0x60))
	{
	    /* LDRH/STRH/LDRSB/LDRSH/LDRD/STRD */
	    int load = (w >> 20) & 1;
	    int sh = (w >> 5) & 3;
	    in->reads = 1u << rn;
	    if (!(w & (1 << 22)))
		in->reads |= 1u << rm;
	    if (!load && sh == 1)
		in->reads |= 1u << rd;			/* STRH */
	    if (!load && sh == 3)
		in->reads |= 3u << rd, in->base = 2;	/* STRD */
	    if (!load && sh == 2)
	    {
		in->base = 2;				/* LDRD */
		in->load_rd = rd + 1;
		return;
	    }
	    if (load)
	    {
		in->load_rd = rd;
		in->load_lat = 2;
		if (rd == 15)
		    in->taken = 4;
	    }
	    return;
	}
	/* Data processing, register operand (or MRS/MSR/CLZ, which cost about the same) */
	{
	    uint32_t op = (w >> 21) & 15;
	    in->reads = 1u << rm;
	    if (op != 13 && op != 15)
		in->reads |= 1u << rn;
	    if ((w & 0x10) && !(w & 0x80))
	    {
		in->reads |= 1u << rs;	/* shift by a register */
		in->base = 2;
	    }
	    if (rd == 15 && !(op >= 8 && op <= 11))
		in->taken = 2;
	}
	return;
    case 1:
	{
	    uint32_t op = (w >> 21) & 15;
	    if (op != 13 && op != 15)
		in->reads |= 1u << rn;
	    if (rd == 15 && !(op >= 8 && op <= 11))
		in->taken = 2;
	}
	return;
    case 2:
    case 3:
	if (((w >> 25) & 7) == 3 && (w & 0x10))
	    return;			/* media/undefined */
	{
	    int load = (w >> 20) & 1;
	    in->reads = 1u << rn;
	    if ((w >> 25) & 1)
		in->reads |= 1u << rm;
	    if (!load)
		in->reads |= 1u << rd;
	    else
	    {
		in->load_rd = rd;
		in->load_lat = (w & (1 << 22)) ? 2 : 1;	/* LDRB */
		if (rd == 15)
		{
		    in->taken = 4;
		    in->load_rd = 0xff;
		}
	    }
	}
	return;
    case 4:
	{
	    int load = (w >> 20) & 1;
	    uint32_t list = w & 0xffff;
	    int n = popcount16(list);
	    in->reads = 1u << rn;
	    in->base = n > 1 ? n : 2;
	    if (!load)
		in->reads |= list;
	    else
	    {
		if (list & 0x8000)
		    in->taken = 4;
		else if (list)
		    in->load_rd = 31 - __builtin_clz(list);
	    }
	}
	return;
    case 5:
	in->taken = 2;			/* B, BL */
	return;
    default:
	return;				/* coprocessor, SWI */
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++)
    {
	struct qemu_plugin_insn *qi = qemu_plugin_tb_get_insn(tb, i);
	uint64_t pc = qemu_plugin_insn_vaddr(qi);
	struct insn *in;
	static GHashTable *by_pc;
	uint32_t w = 0;
	size_t size;

	if (!by_pc)
	    by_pc = g_hash_table_new(g_int64_hash, g_int64_equal);
	in = g_hash_table_lookup(by_pc, &pc);
	if (!in)
	{
	    in = g_new0(struct insn, 1);
	    in->pc = pc;
	    size = qemu_plugin_insn_data(qi, &w, sizeof w);
	    if (qemu_plugin_insn_size(qi) == 4 && size == 4)
		decode_arm(in, w);
	    else
	    {
		in->thumb = 1;
		in->base = 1;
		in->load_rd = 0xff;
		in->taken = 2;
	    }
	    g_hash_table_insert(by_pc, &in->pc, in);
	    g_ptr_array_add(all_insns, in);
	}
	qemu_plugin_register_vcpu_insn_exec_cb(qi, vcpu_insn, QEMU_PLUGIN_CB_NO_REGS, in);
	qemu_plugin_register_vcpu_mem_cb(qi, vcpu_mem, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, in);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    FILE *f = fopen(out_path, "w");
    if (!f)
	return;
    fprintf(f, "model fill %" PRIu64 " write %" PRIu64 " word %" PRIu64 " writeback %" PRIu64 "\n",
	    p_fill, p_write, p_word, p_writeback);
    fprintf(f, "cycles %" PRIu64 " instructions %" PRIu64 " imiss %" PRIu64 " dmiss %" PRIu64
	    " storemiss %" PRIu64 " wbstall %" PRIu64 " fillwait %" PRIu64 "\n",
	    now - stat_cycles_start, stat_insns, stat_imiss, stat_dmiss, stat_smiss, stat_wbstall,
	    stat_fill_wait);
    fprintf(f, "frames");
    for (guint i = 1; i < frame_cycles->len; i++)
	fprintf(f, " %" PRIu64, g_array_index(frame_cycles, uint64_t, i) -
		g_array_index(frame_cycles, uint64_t, i - 1));
    fprintf(f, "\n");
    fprintf(f, "frame_insns");
    for (guint i = 1; i < frame_insns->len; i++)
	fprintf(f, " %" PRIu64, g_array_index(frame_insns, uint64_t, i) -
		g_array_index(frame_insns, uint64_t, i - 1));
    fprintf(f, "\n");
    for (guint i = 0; i < all_insns->len; i++)
    {
	struct insn *in = g_ptr_array_index(all_insns, i);
	if (in->count)
	    fprintf(f, "%" PRIx64 " %" PRIu64 " %" PRIu64 "\n", in->pc, in->count, in->cycles);
    }
    fclose(f);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
					   int argc, char **argv)
{
    for (int i = 0; i < argc; i++)
    {
	char *eq = strchr(argv[i], '=');
	if (!eq)
	    continue;
	*eq = 0;
	const char *v = eq + 1;
	if (!strcmp(argv[i], "out"))
	    out_path = g_strdup(v);
	else if (!strcmp(argv[i], "mark"))
	    mark_addr = strtoull(v, NULL, 16);
	else if (!strcmp(argv[i], "fill"))
	    p_fill = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "write"))
	    p_write = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "word"))
	    p_word = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "writeback"))
	    p_writeback = strtoull(v, NULL, 0);
	else
	{
	    fprintf(stderr, "arm926: unknown argument %s\n", argv[i]);
	    return -1;
	}
    }
    if (!mark_addr)
	counting = 1;
    cache_init(&icache, 16 * 1024, 4);
    cache_init(&dcache, 8 * 1024, 4);
    frame_cycles = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    frame_insns = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    all_insns = g_ptr_array_new();
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
