#include "../../emu_interface.h"


extern "C" int mainEntry(int argc, char** argv);

using namespace std;

class PocketSnesEmulator : public CoreInterface
{
	private:

	public:
		PocketSnesEmulator()
		{
      registerInformations(CONSOLE_SUPER_NINTENDO, "pocketsnes", "PocketSNES", "1.0");
      registerExtension("smc");
		}

		virtual void run(int argc, char **argv) { mainEntry(argc, argv); }
};

static PocketSnesEmulator emulator;

extern "C" CoreInterface* retrieve()
{
	return &emulator;
}
