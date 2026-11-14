# Voice lines: the MP3 decoded on the main thread

Status: knowledge, written 2026-11-13. Marks: **read** = read in the instructions of Oblivion.exe 1.2.0.416 (the
address given), **measured** = in the running game (the run given), **derived**, **unverified**.

## What the game does

- **The voice player is 0x006A9E00** (read; `__thiscall(manager; IDirectSoundBuffer** out, const char* path, byte
  flags)`, `RET 0Ch`). Its one caller is 0x006AC610 (the sound's creation by path), which sends every path with
  "voice" in it here and takes the returned AX as the line's duration in ms into `sound+0x1C`; 0 means failure and
  the WAV loader 0x006AABD0 is tried (read 0x006AC7F7-0x006AC80E). Ghidra shows the function as void - wrong.
- **Its steps** (read 0x006A9E00-0x006AA15E): the path copied to a 260-byte stack buffer; the first ".wav" made
  ".mp3"; the file looked for through the pointer at 0x00B27E50 (`bool __cdecl(path, 0)`, loose or in an archive);
  the file object 0x006B45C0 (0x1C bytes) and the decoder 0x006B1D40 (0x413C bytes) made; a DirectSound buffer
  created (`[manager+8]` is the IDirectSound; PCM, one channel, 16 bit, the file's rate; the flags 0xA0 =
  DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLVOLUME, with 0x20010 = DSBCAPS_CTRL3D | DSBCAPS_MUTE3DATMAXDISTANCE more when
  the call's flags have bit 1; the bytes = frames * 0x900), locked whole (DSBLOCK_ENTIREBUFFER); then **every frame
  decoded in a loop** - 0x006B3AE0 returns one frame of 0x900 bytes, null at the end, with no bound against the
  buffer - unlocked, the two objects destroyed. The returned duration is `((bytes >> 1) / (rate / 1000))`, its low
  16 bits.
- **The file object** (read 0x006B4500-0x006B45B7) reads the whole file at its construction through 0x00431130 and
  closes it: `+0x00` the frame count, `+0x04` the header struct (`+8` the sample rate), `+0x08` the bytes, `+0x0C`
  their count, `+0x18` a byte "header valid". **The frame count is the number of FF FB byte pairs in the whole file**
  (0x006B4590) - the buffer's size and the line's duration come from it.
- **The decoder** takes MPEG-1 Layer III, mono, 32-64 kbit/s, 44100 / 48000 / 32000 Hz only (read
  0x006B37A0-0x006B38A9); anything else is "not compatible" and falls to the WAV loader. Its state is all in its heap
  objects; the tables it reads are constant or written once at the process's start (read: a sweep of
  0x006AF660-0x006AF800 and 0x006B1D40-0x006B6700). It has no user but 0x006A9E00 (read: every call site).
- **The sound's destructor 0x006B6700** releases the buffer at `sound+0x50` until its count is 0 (read
  0x006B6732-0x006B673F): a reference of one's own does not keep a buffer alive.
- **Who plays**: three of the four voice callers call Play in the same call chain (0x004D9100, 0x005F70F0,
  0x0065A8B0); the dialogue's 0x005F7310 queues a timed play, which the manager's update 0x006AE860 may correct by a
  SetCurrentPosition when it is more than 45 ms late. Nothing reads the buffer's contents (read; the lip sync loads
  its own file).

## What it costs (measured 2026-11-13, Linux, runs obng-stress-ride/20261113-205845 and -210328)

75 calls of 0x006A9E00 over 1 ms in two flights, 420 ms together: median 3.9 ms, mean 5.6, the longest 27.8 ms - with
the line's length (a goodbye 3 ms, an answer about the news 12, a rumour 14). All of them the game's own voices
(oblivion.esm 72, knights.esp 3): greetings, idle conversations between actors, a few combat sounds. A process
manager call over 8 ms was this in about two thirds of the cases outside one actor's controller fault
(docs/performance-backlog.md, 2026-11-13).

## The route: `[Route] VoiceDecode` (plugin/src/VoiceDecode.cpp, src/engine/sound/)

The same buffer and the same returned duration; the line's first `VoiceDecodeLeadMs` (150) decoded at the call, the
rest by one worker thread that writes it into the buffer while the line plays, eight frames a lock.

- Mode 1 runs the game's own decoder on the worker, with the calling thread's x87 control word and MXCSR (class B).
  Mode 2 decodes with minimp3 (third_party/minimp3, CC0; class C). Mode 2 is the default (Nadi, 2026-11-13).
- **The lead** is there because DirectSound plays what lies between its play and write cursors as it stood
  (Microsoft Learn, "Play and Write Cursors"): the line's start must be in the buffer before Play.
- **The destructor is hooked**: a line in work whose buffer is the dying sound's (COM identity: both asked for
  IUnknown) is cancelled, and the canceller waits out the worker's current lock.
- What a line holds (the file object, the decoder) is freed by the thread that calls the voice player or destroys
  a sound, never by the worker.
- Calls the route leaves to the game untouched: no MP3 of that name, an MP3 the game's decoder does not take, a path
  that does not fit the game's buffer. With every slot taken (eight) a line is decoded whole at the call.
- `VoiceDecodeCheck=1` (mode 2): the worker decodes every line once more with both decoders and writes the largest
  difference and the mean square - the comparison class C asks for.

**Run in the game on Windows, 2026-11-13 21:44-22:23** (Dream Air session, mode 2, measured from its `OBNG.log`; docs/performance-backlog.md 2026-11-14): 310 lines through the route, 0.50 ms a call on the mean and 1.19 ms the longest, 37 334 frames decoded on the worker into playing buffers, no cancel, no lock failure, no crash in 38 minutes. 45 lines were left to the game's function at 9.4-12.6 ms each - derived, not verified: lines without a voice file, for which Elys' Universal Silent Voice plays `Data\OBSE\Plugins\Elys_USV.mp3` (8.17 s, 313 frames); the route's existence check does not see that swap. **Not verified**: mode 2's samples against the game's (`VoiceDecodeCheck`), the line's start against the lips (no defect named by Nadi, not stated as accepted).
Open in particular: DirectSound's Lock and Unlock from a second thread while the main thread plays or stops the
buffer (Windows and Proton), the timing of a line's start against its lip movement, and mode 2's samples against
the game's (the check).
