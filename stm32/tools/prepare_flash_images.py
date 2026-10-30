"""Prepare the existing board's XIP flash images. Does not connect to or program a board."""
from pathlib import Path
import argparse,shutil,subprocess
r=Path(__file__).resolve().parents[1]
a=argparse.ArgumentParser(description=__doc__)
a.add_argument('--config',choices=['SimulationFlash','Production'],default='SimulationFlash')
a.add_argument('--signer',required=True,help='Full path to STM32_SigningTool_CLI.exe (STM32CubeProgrammer)')
a.add_argument('--objcopy',help='Full path to arm-none-eabi-objcopy executable')
args=a.parse_args()
objcopy=args.objcopy or shutil.which('arm-none-eabi-objcopy')
if not objcopy:
 candidates=list((r/'tools/toolchain').rglob('arm-none-eabi-objcopy.exe'))+list((r.parent/'.build-tools').rglob('arm-none-eabi-objcopy.exe'))
 if candidates:objcopy=str(candidates[0])
if not objcopy:raise SystemExit('Pass --objcopy PATH')
app=r/'build/Appli'/args.config/'996_AI_Product_Inspection_Appli.elf'
fsbl=r/'build/FSBL/Release/996_AI_Product_Inspection_FSBL.bin'
for f in [app,fsbl,Path(args.signer)]:
 if not f.is_file():raise SystemExit('Missing required input: '+str(f))
out=r/'build/flash'/args.config;out.mkdir(parents=True,exist_ok=True)
signed=out/'fsbl-trusted.bin'
subprocess.run([args.signer,'-bin',str(fsbl),'-nk','-of','0x80000000','-t','fsbl','-o',str(signed),'-hv','2.3'],check=True)
subprocess.run([objcopy,'-I','binary',str(signed),'--change-addresses','0x70000000','-O','ihex',str(out/'fsbl.hex')],check=True)
subprocess.run([objcopy,'-O','ihex',str(app),str(out/'appli.hex')],check=True)
print('Prepared',out)
print('No board was flashed. Review README before programming.')
