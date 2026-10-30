"""Compile portable production logic and the unmodified IMX6ULL receiver; run deterministic tests."""
from pathlib import Path
import argparse, json, shutil, subprocess, time
r=Path(__file__).resolve().parents[1]
p=r/'SoftwarePackage/Projects/99_Applications/996_AI_Product_Inspection/Appli/Core'
a=argparse.ArgumentParser();a.add_argument('--cc',default=shutil.which('gcc'));args=a.parse_args()
if not args.cc: raise SystemExit('A host GCC compiler is required; pass --cc PATH')
exe=r/'reports/test_inspection.exe'
cmd=[args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I'+str(p/'Inc'),'-I'+str(r/'tests'),str(p/'Src/inspection_core.c'),str(r/'tests/imx6ull_inspect.c'),str(r/'tests/test_inspection.c'),'-lm','-o',str(exe)]
subprocess.run(cmd,check=True)
t=time.monotonic();completed=subprocess.run([str(exe)],capture_output=True,text=True)
print(completed.stdout,end='');print(completed.stderr,end='')
(r/'reports/host_tests.json').write_text(json.dumps({'passed':completed.returncode==0,'compiler':args.cc,'command':cmd,'seconds':time.monotonic()-t,'output':completed.stdout,'scope':'Host logic and byte-level receiver interoperability; NOT on-board camera/NPU/CAN validation'},indent=2),encoding='utf-8')

if completed.returncode:raise SystemExit(completed.returncode)
can_exe=r/'reports/test_can_backend.exe'
can_cmd=[args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I'+str(r/'tests/mock'),'-I'+str(r/'tests'),'-I'+str(p/'Inc'),str(p/'Src/inspection_core.c'),str(p/'Src/inspection_can.c'),str(r/'tests/imx6ull_inspect.c'),str(r/'tests/test_can_backend.c'),'-lm','-o',str(can_exe)]
subprocess.run(can_cmd,check=True)
can=subprocess.run([str(can_exe)],capture_output=True,text=True)
print(can.stdout,end='');print(can.stderr,end='')
(r/'reports/can_backend_tests.json').write_text(json.dumps({'passed':can.returncode==0,'command':can_cmd,'output':can.stdout,'scope':'Real CAN backend with mocked HAL; NOT a physical bus test'},indent=2),encoding='utf-8')

if can.returncode:raise SystemExit(can.returncode)
sim_cmd=can_cmd.copy();sim_cmd.insert(1,'-DINSPECTION_SIMULATION=1')
sim_cmd[-1]=str(r/'reports/test_can_simulation.exe')
subprocess.run(sim_cmd,check=True)
sim=subprocess.run([sim_cmd[-1]],capture_output=True,text=True)
print(sim.stdout,end='');print(sim.stderr,end='')
(r/'reports/can_simulation_tests.json').write_text(json.dumps({'passed':sim.returncode==0,'command':sim_cmd,'output':sim.stdout},indent=2),encoding='utf-8')
if sim.returncode:raise SystemExit(sim.returncode)
cam_exe=r/'reports/test_camera_buffers.exe'
cam_cmd=[args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I'+str(r/'tests/camera_mock'),'-I'+str(p/'Inc'),str(p/'Src/inspection_camera.c'),str(r/'tests/test_camera_buffers.c'),'-o',str(cam_exe)]
subprocess.run(cam_cmd,check=True)
cam=subprocess.run([str(cam_exe)],capture_output=True,text=True)
print(cam.stdout,end='');print(cam.stderr,end='')
(r/'reports/camera_buffer_tests.json').write_text(json.dumps({'passed':cam.returncode==0,'command':cam_cmd,'output':cam.stdout,'scope':'Real camera buffer manager with mocked ISR/HAL; NOT a physical camera test'},indent=2),encoding='utf-8')
raise SystemExit(cam.returncode)


