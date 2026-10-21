/*
 * Copyright © 2011-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LUX_COOP_RECORDER_H
#define LUX_COOP_RECORDER_H

//----------------------------------------------

#include "LuxBase.h"

//----------------------------------------------

class cLuxCoopRecorderImpl;

/**
 * Records the game window and what the PC plays to an MP4 (H.264 video, AAC audio), with the
 * encoders that come with Windows (Media Foundation) and WASAPI loopback for the sound.
 * Frames are read back from the back buffer after rendering, at most afFps per second.
 * Only on Windows; elsewhere Start just fails.
 */
class cLuxCoopRecorder
{
public:
	cLuxCoopRecorder();
	~cLuxCoopRecorder();

	bool Start(const tString& asFile, int alWidth, int alHeight, int alFps);
	void Stop();
	bool IsRecording();
	//Also save a BMP every so many seconds next to the video, in <name>_frames (0: none)
	void SetStills(float afEvery);

	//Call after the frame is rendered, before the buffers are swapped
	void CaptureFrame();

private:
	cLuxCoopRecorderImpl *mpImpl;
};

//----------------------------------------------

#endif // LUX_COOP_RECORDER_H
