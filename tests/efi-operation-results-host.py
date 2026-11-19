"""Compile actual production EFI request functions against deterministic firmware faults."""
from pathlib import Path
import os
import subprocess
import argparse
ROOT=Path(__file__).resolve().parents[1]
def extract(path,name):
    text=path.read_text(encoding='utf-8-sig')
    start=text.index('static efi_status '+name+'(')
    body=text.index('{',start);depth=1;at=body+1
    while depth:
        if text[at]=='{':depth+=1
        elif text[at]=='}':depth-=1
        at+=1
    return text[start:at]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--emit-only',action='store_true');args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'tests/EFIOperationResultsHarness.cpp.in').read_text(encoding='utf-8-sig')
    source=source.replace('// ACTUAL_PRODUCTION_FUNCTIONS',extract(ROOT/'src/efi/FirmwareSettings.h','ProcessSettings')+'\n'+extract(ROOT/'src/efi/HAKBootEFI.cpp','StartRequested'))
    target=args.output/'efi-operation-results.cpp';target.write_text(source,encoding='utf-8')
    if args.emit_only:return
    vc=Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231')
    sdk=Path('C:/Program Files (x86)/Windows Kits/10');bin=vc/'bin/Hostx64/x64'
    env=os.environ.copy();env['PATH']=str(bin)+os.pathsep+env['PATH']
    exe=args.output/'efi-operation-results.exe'
    subprocess.run([str(bin/'cl.exe'),'/nologo','/EHsc','/std:c++17','/utf-8','/W4','/WX',f'/I{vc}/include',f'/I{sdk}/Include/10.0.26100.0/ucrt',f'/I{ROOT}/src/efi',str(target),f'/Fe:{exe}',f'/Fo:{args.output}/efi-operation-results.obj','/link',f'/LIBPATH:{vc}/lib/x64',f'/LIBPATH:{sdk}/Lib/10.0.26100.0/ucrt/x64',f'/LIBPATH:{sdk}/Lib/10.0.26100.0/um/x64'],env=env,check=True)
    subprocess.run([str(exe.resolve())],check=True)
if __name__=='__main__':main()
