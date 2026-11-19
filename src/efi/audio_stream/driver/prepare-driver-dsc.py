"""Keep pinned upstream library/PCD/build settings; build AudioDxe alone."""
from pathlib import Path
import sys
workspace=Path(sys.argv[1]).resolve()
source=workspace/'OpenCorePkg/OpenCorePkg.dsc'
text=source.read_text()
start=text.index('[Components]');end=text.index('[LibraryClasses]',start)
text=text[:start]+'[Components]\n  OpenCorePkg/Staging/AudioDxe/AudioDxe.inf\n\n'+text[end:]
text='\n'.join(line for line in text.splitlines() if 'FLASH_DEFINITION' not in line
    and not ('!include' in line and ('NetworkPkg/' in line or 'Ext4Pkg/' in line)))+'\n'
text=text.replace('PLATFORM_NAME           = OpenCorePkg','PLATFORM_NAME           = HakAudioDriver')
(workspace/'OpenCorePkg/HakAudioDriver.dsc').write_text(text,newline='\n')
