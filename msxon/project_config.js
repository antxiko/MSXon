DoClean   = false;
DoCompile = true;
DoMake    = true;
DoPackage = true;
DoDeploy  = true;
DoRun     = false;

ProjName = "msxon";
ProjModules = [ ProjName ];
LibModules = [ "system", "bios", "vdp", "print", "input", "psg", "memory", "dos", "tool/qrcode_tiny" ];
AddSources = [ "../../engine/src/network/unapi_tcp.asm" ];
Machine = "2";
Target = "DOS2";
DiskSize = "720K";

AppSignature = true;
AppCompany = "AX";
AppID = "MN";

Verbose = true;
CompileComplexity = "Default";

ForceRamAddr = 0xC000;
