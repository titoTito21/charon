@echo off
rem Charon: NVDA speaks in Windows Setup. If it cannot, Narrator does: never silence.
rem Run by the answer file's windowsPE pass from \charon\pe on the install media.
rem @LANG@ is replaced by Charon: en, or pl when the Windows being installed is Polish.
set P=%~dp0
set N=X:\CharonNvda
set C=X:\CharonNvdaConfig
set S=X:\Windows\System32
if exist %C%\charon.log exit /b 0
md %C% 2>nul
echo start %date% %time% > %C%\charon.log
rem NVDA's installer is a 32-bit program and Setup runs none: nvdape.exe unpacks it (into memory disk X:).
for %%f in ("%P%..\readers\nvda_*.exe") do if not exist %N%\nvda_noUIAccess.exe "%P%nvdape.exe" "%%~ff" %N% @LANG@
echo unpacked %errorlevel% >> %C%\charon.log
if not exist %N%\nvda_noUIAccess.exe goto narrator
rem System libraries NVDA binds and Setup lacks get a stand-in; NVDA's 32-bit helper loader too.
for %%n in (bthprops.cpl sas.dll magnification.dll winusb.dll pdh.dll d3d11.dll) do if not exist %S%\%%n copy /y "%P%pestub.dll" %N%\%%n >nul
for /d %%v in (%N%\lib\*) do (md %%v\x86 2>nul & copy /y "%P%pewait.exe" %%v\x86\nvdaHelperRemoteLoader.exe >nul)
copy /y "%P%nvda.ini" %C%\nvda.ini >nul
rem Setup plays only sound of the categories "alerts" and "sound effects" (measured): NVDA as it is stays silent.
rem The synthesizer driver charonpe (eSpeak NG) loads peaudio.dll, which gives NVDA's sound the category "alerts".
md %C%\scratchpad\synthDrivers 2>nul
copy /y "%P%charonpe.py" %C%\scratchpad\synthDrivers\charonpe.py >nul
copy /y "%P%peaudio.dll" %C%\peaudio.dll >nul
net start audiosrv >nul 2>nul
start "" %N%\nvda_noUIAccess.exe -c %C% -f %C%\nvda.log
rem NVDA counts as speaking when its log says the category is set and names the synthesizer it loaded (up to a minute).
for /l %%i in (1,1,30) do (
  ping -n 2 127.0.0.1 >nul
  find "Charon: Setup audio category set" %C%\nvda.log >nul 2>nul && find "Loaded synthDriver charonpe" %C%\nvda.log >nul 2>nul && goto speaks
)
echo NVDA did not load its synthesizer or its sound stays silent >> %C%\charon.log
%N%\nvda_noUIAccess.exe -q
:narrator
echo Narrator instead >> %C%\charon.log
start "" %S%\Narrator.exe
exit /b 1
:speaks
echo NVDA speaks %time% >> %C%\charon.log
exit /b 0
