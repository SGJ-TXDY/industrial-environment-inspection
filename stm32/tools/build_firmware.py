"""Reproducible GNU Arm build from the checked-in CubeIDE XML (no IDE or generated makefiles needed)."""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, re, shutil, subprocess, sys, time
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
PROJECT=ROOT/'SoftwarePackage/Projects/99_Applications/996_AI_Product_Inspection'
def linked_paths(ide):
    root=ET.parse(ide/'.project').getroot(); links={}
    for n in root.findall('./linkedResources/link'):
        uri=n.findtext('locationURI'); m=re.match(r'PARENT-(\d+)-PROJECT_LOC/(.*)',uri)
        if not m:raise RuntimeError('Unsupported resource '+str(uri))
        target=ide
        for _ in range(int(m[1])): target=target.parent
        links[n.findtext('name')]=(target/m[2]).resolve()
    return links

def build(part,config,cc,jobs):
    ide=PROJECT/'STM32CubeIDE'/part; links=linked_paths(ide)
    configs=ET.parse(ide/'.cproject').getroot().findall('./storageModule/cconfiguration')
    chosen=next((c for c in configs if c.find('./storageModule/configuration').get('name')==config),None)
    if chosen is None: raise RuntimeError(f'{part}: configuration {config} is not present')
    opt={o.get('superClass'):o for o in chosen.iter('option')}
    def option(suffix):return next((v for k,v in opt.items() if k and k.endswith(suffix)),None)
    def values(suffix):
        v=option(suffix);return [] if v is None else [n.get('value') for n in v.findall('listOptionValue')]
    def resolve(value):
        value=value.strip('"');m=re.fullmatch(r'\$\{workspace_loc:/\$\{ProjName\}/(.*)\}',value)
        if m:
            name=m[1]
            for key in sorted(links,key=len,reverse=True):
                if name==key or name.startswith(key+'/'):return links[key]/name[len(key):].lstrip('/')
            return ide/name
        return (ide/config/value).resolve()
    includes=[resolve(v) for v in values('c.compiler.option.includepaths')]
    missing=[str(x) for x in includes if not x.is_dir()]
    if missing:raise RuntimeError('Missing include paths: '+repr(missing))
    sources=[v for k,v in links.items() if v.suffix.lower()=='.c']
    # The linked generated-model folder may contain user supplied network.c and extra generated C files.
    if part=='Appli':sources+=sorted((PROJECT/'Model/Generated').glob('*.c'))
    sources+=sorted((ide/'Application').rglob('*.c'))+sorted((ide/'Application').rglob('*.s'))
    sources=list(dict.fromkeys(sources))
    missing=[str(s) for s in sources if not s.is_file()]
    if missing:raise RuntimeError('Missing sources: '+repr(missing))
    builddir=ROOT/'build'/part/config;objdir=builddir/'obj';objdir.mkdir(parents=True,exist_ok=True)
    cpu=['-mcpu=cortex-m55','-mfpu=fpv5-d16','-mfloat-abi=hard','-mthumb','-mcmse']
    flags=cpu+['-std=gnu11','-O2','-g3','-ffunction-sections','-fdata-sections','-Wall','-Wextra','--specs=nano.specs']
    flags+=['-D'+v for v in values('c.compiler.option.definedsymbols')]
    flags+=['-I'+str(x) for x in includes]
    commands=[];objects=[]
    for s in sources:
        obj=objdir/(s.stem+'_'+hashlib.sha1(str(s).encode()).hexdigest()[:10]+'.o');objects.append(obj)
        cmd=[str(cc)]+flags+(['-x','assembler-with-cpp'] if s.suffix.lower()=='.s' else [])+['-c',str(s),'-o',str(obj)]
        commands.append({'directory':str(builddir),'file':str(s),'arguments':cmd})
    (builddir/'compile_commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
    print(f'Compiling {part}/{config}: {len(sources)} units',flush=True)
    def compile_one(c):
        result=subprocess.run(c['arguments'],capture_output=True,text=True,errors='replace')
        return c,result
    errors=[];log=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for c,result in pool.map(compile_one,commands):
            if result.stdout or result.stderr:log.append(c['file']+'\n'+result.stdout+result.stderr)
            if result.returncode:errors.append(c['file']);print(result.stderr,flush=True)
    (builddir/'compile.log').write_text('\n'.join(log),encoding='utf-8')
    if errors:raise RuntimeError(f'{len(errors)} compile units failed; see {builddir}/compile.log')
    script=resolve(option('c.linker.option.script').get('value'))
    elf=builddir/('996_AI_Product_Inspection_'+part+'.elf')
    response=builddir/'link.rsp'
    def q(x):return '"'+str(x).replace('\\','/')+'"'
    args=[*map(str,objects),*cpu,'-T'+str(script),'--specs=nosys.specs','--specs=nano.specs','-static','-Wl,--gc-sections','-Wl,--no-warn-rwx-segments','-Wl,-Map='+str(elf.with_suffix('.map'))]
    if part=='Appli':args+=['-Wl,--cmse-implib','-Wl,--out-implib='+str(builddir/'secure_nsclib.o'),'-u','_printf_float']
    args+=['-L'+str(resolve(v)) for v in values('c.linker.option.directories')]
    args+=['-Wl,--start-group']+['-l'+v for v in values('c.linker.option.libraries')]+['-lc','-lm','-Wl,--end-group','-o',str(elf)]
    response.write_text('\n'.join(q(x) for x in args),encoding='utf-8')
    result=subprocess.run([str(cc),'@'+str(response)],capture_output=True,text=True,errors='replace')
    (builddir/'link.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:print(result.stderr);raise RuntimeError('Link failed')
    prefix=str(cc).removesuffix('gcc.exe').removesuffix('gcc')
    suffix='.exe' if cc.suffix=='.exe' else ''
    for fmt,ext in [('binary','.bin'),('ihex','.hex')]:
        subprocess.run([prefix+'objcopy'+suffix,'-O',fmt,str(elf),str(elf.with_suffix(ext))],check=True)
    size=subprocess.check_output([prefix+'size'+suffix,str(elf)],text=True)
    print(size,flush=True)
    report={'part':part,'configuration':config,'compiled_units':len(sources),'compiler':str(cc),'elf':str(elf),'size':size,'hardware_tested':False}
    (builddir/'build_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',help='Path to arm-none-eabi-gcc executable')
    parser.add_argument('--config',choices=['Simulation','SimulationFlash','Production','All'],default='All')
    parser.add_argument('--jobs',type=int,default=min(8,os.cpu_count() or 2))
    args=parser.parse_args();candidate=args.cc or shutil.which('arm-none-eabi-gcc')
    if not candidate:
        found=list((ROOT/'tools/toolchain').rglob('arm-none-eabi-gcc.exe'))+list((ROOT.parent/'.build-tools').rglob('arm-none-eabi-gcc.exe'))
        if found:candidate=str(found[0])
    if not candidate:raise SystemExit('GNU Arm compiler not found; pass --cc PATH. CubeIDE can also import this project.')
    cc=Path(candidate);reports=[]
    for c in (['Simulation','SimulationFlash','Production'] if args.config=='All' else [args.config]):reports.append(build('Appli',c,cc,args.jobs))
    reports.append(build('FSBL','Release',cc,args.jobs))
    (ROOT/'reports/firmware_builds.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
if __name__=='__main__':main()
