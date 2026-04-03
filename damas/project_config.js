DoClean   = false;
DoCompile = true;
DoMake    = true;
DoPackage = true;
DoDeploy  = true;
DoRun     = false;

ProjName = "damas";
ProjModules = [ ProjName ];
LibModules = [ "system", "bios", "vdp", "print", "input", "memory", "dos" ];
AddSources = [ "../../engine/src/network/unapi_tcp.asm" ];
Machine = "2";
Target = "DOS2";
DiskSize = "720K";

AppSignature = true;
AppCompany = "AX";
AppID = "DM";

Verbose = true;
CompileComplexity = "Default";

ForceRamAddr = 0x8000;
