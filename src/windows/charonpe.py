# Charon: NVDA's synthesizer inside Windows Setup (r407). eSpeak NG, unchanged, under another name:
# loading this driver is the first moment code of Charon runs inside NVDA, before any speech
# stream exists. Windows Setup's WinPE plays only audio streams of the categories "alerts" and
# "sound effects" (measured: everything else is silence), and NVDA gives its streams none, so
# peaudio.dll (src/windows/peaudio.c) sets the category for every stream NVDA makes from here on.
# nvda-setup.cmd puts this file into the scratchpad of NVDA's Setup configuration and waits for
# the line logged below; without it Narrator speaks instead. GPL-2.0-or-later.
import ctypes
import os
import time

import globalVars
from logHandler import log
from synthDrivers.espeak import SynthDriver as _Espeak

_library = None


def _makeAudible():
	global _library
	_library = ctypes.CDLL(os.path.join(globalVars.appArgs.configPath, "peaudio.dll"))
	_library.CharonPeAudio.restype = ctypes.c_long
	result = -1
	for _attempt in range(40):  # the sound service may still be starting: up to 10 seconds
		result = _library.CharonPeAudio()
		if result == 0:
			break
		time.sleep(0.25)
	if result != 0:
		log.error("Charon: Setup audio category not set, result 0x%08x" % (result & 0xFFFFFFFF))
		return
	# The stream for beeps was made before this driver was loaded: make it again.
	try:
		import tones

		tones.terminate()
		tones.initialize()
	except Exception:
		log.error("Charon: beeps not reopened", exc_info=True)
	log.info("Charon: Setup audio category set")


_makeAudible()


class SynthDriver(_Espeak):
	name = "charonpe"
	description = "eSpeak NG"
