/*
	SuperCollider real time audio synthesis system
    Copyright (c) 2002 James McCartney. All rights reserved.
	http://www.audiosynth.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include "SC_PlugIn.h"

#include <algorithm>            /* for std::min and std::max */

#include <ableton/Link.hpp>

#ifdef NOVA_SIMD
#include "simd_memory.hpp"

#include "function_attributes.h"

#endif

static InterfaceTable *ft;

////////////////////////////////////////////////////////////////////////////////////////////////

struct LinkUGen : public Unit
{
  static void LinkUgen_Ctor()
  {
    if (sLink != nullptr && !sLink->isEnabled())
    {
      sLink->enable(true);
    }
    ++sNumInstances;
  }

  static void LinkUGen_Dtor()
  {
    --sNumInstances;
    if (sNumInstances == 0)
    {
      sLink->enable(false);
    }
  }

  static std::unique_ptr<ableton::Link> sLink;
  static uint32 sNumInstances;
};

struct LinkTrig : public LinkUGen
{
  double mBeats;
  double mBeatTimeOffset;
};

struct LinkPhase : public LinkUGen
{
  std::chrono::microseconds mTime;
};

std::unique_ptr<ableton::Link> LinkUGen::sLink =
  std::unique_ptr<ableton::Link>(new ableton::Link(120.0));
uint32 LinkUGen::sNumInstances = 0;

extern "C"
{
	void LinkTrig_Ctor(LinkTrig *unit);
  void LinkTrig_Dtor(LinkTrig *unit);
	void LinkTrig_next(LinkTrig *unit, int inNumSamples);

  void LinkPhase_Ctor(LinkPhase *unit);
  void LinkPhase_Dtor(LinkPhase *unit);
  void LinkPhase_next(LinkPhase *unit, int inNumSamples);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef NOVA_SIMD
FLATTEN void LinkTrig_next_nova(LinkTrig *unit, int inNumSamples);
FLATTEN void LinkTrig_next_k_nova(LinkTrig *unit, int inNumSamples);
FLATTEN void LinkPhase_next_nova(LinkPhase *unit, int inNumSamples);
FLATTEN void LinkPhase_next_k_nova(LinkPhase *unit, int inNumSamples);
#endif

// LinkTrig

void LinkTrig_Ctor(LinkTrig *unit)
{
  LinkUGen::LinkUgen_Ctor();

  float *quantum = IN(0);

  const auto bufferTimeOffset = std::chrono::microseconds(llround(
    unit->mWorld->mBufFramePos * unit->mRate->mSampleDur * 1e6));

  const auto timeAtBufferBegin =
    unit->sLink->clock().ticksToMicros(unit->mWorld->mAudioHostTime) + bufferTimeOffset;

  auto timeline = unit->sLink->captureAudioTimeline();

  if (unit->sNumInstances == 0)
  {
    timeline.requestBeatAtTime(0, timeAtBufferBegin, *quantum);
    unit->sLink->commitAudioTimeline(timeline);
    unit->mBeatTimeOffset = 0;
  }
  else
  {
    const auto currentBeats = timeline.beatAtTime(timeAtBufferBegin, *quantum);
    unit->mBeatTimeOffset = currentBeats + *quantum - std::fmod(currentBeats, *quantum);
  }

  SETCALC(LinkTrig_next);
  ZOUT0(0) = 0;
}

void LinkTrig_Dtor(LinkTrig *unit)
{
  LinkUGen::LinkUGen_Dtor();
}

void LinkTrig_next(LinkTrig *unit, int inNumSamples)
{
  float *quantum = IN(0);
  float *beatsPerClick = IN(1);
  float *out = ZOUT(0);

  LOOP1(inNumSamples,
    ZXP(out) = 0.0f;
  )

  if (unit->sLink != nullptr)
  {
    const auto bufferDuration = std::chrono::microseconds(llround(
      unit->mRate->mSampleDur * static_cast<double>(inNumSamples) * 1e6));

    const auto bufferTimeOffset = std::chrono::microseconds(llround(
      unit->mWorld->mBufFramePos * unit->mRate->mSampleDur * 1e6));

    const auto timeAtBufferBegin =
      unit->sLink->clock().ticksToMicros(unit->mWorld->mAudioHostTime) + bufferTimeOffset;

    const auto timeAtBufferEnd = timeAtBufferBegin + bufferDuration;

    auto timeline = unit->sLink->captureAudioTimeline();

    const auto beatsAtBufferBegin = unit->mBeats;
    const auto beatsAtBufferEnd =
      timeline.beatAtTime(timeAtBufferEnd, *quantum) - unit->mBeatTimeOffset;

    unit->mBeats = beatsAtBufferEnd;

    const double beatsInBuffer = beatsAtBufferEnd - beatsAtBufferBegin;
    const double samplesPerBeat = static_cast<double>(inNumSamples) / beatsInBuffer;

    double clickAtPosition =
      beatsAtBufferBegin - fmod(beatsAtBufferBegin, *beatsPerClick);

    while (clickAtPosition < beatsAtBufferEnd && beatsAtBufferEnd > 0)
    {
      const long offset = lround(samplesPerBeat * (clickAtPosition - beatsAtBufferBegin));
      if (offset >= 0 && offset < (long)(inNumSamples))
      {
        unit->mOutBuf[0][offset] = 1.f;
      }
      clickAtPosition += *beatsPerClick;
    }
  }
}

// LinkPhase

void LinkPhase_Ctor(LinkPhase *unit)
{
  LinkUGen::LinkUgen_Ctor();

  float *quantum = IN(0);

  const auto bufferTimeOffset = std::chrono::microseconds(llround(
    unit->mWorld->mBufFramePos * unit->mRate->mSampleDur * 1e6));

  const auto timeAtBufferBegin =
    unit->sLink->clock().ticksToMicros(unit->mWorld->mAudioHostTime) + bufferTimeOffset;

  auto timeline = unit->sLink->captureAudioTimeline();

  if (unit->sNumInstances == 0)
  {
    timeline.requestBeatAtTime(0, timeAtBufferBegin, *quantum);
    unit->sLink->commitAudioTimeline(timeline);
  }

  unit->mTime = timeAtBufferBegin;

  SETCALC(LinkPhase_next);
  ZOUT0(0) = 0;
}

void LinkPhase_Dtor(LinkPhase *unit)
{
  LinkUGen::LinkUGen_Dtor();
}

void LinkPhase_next(LinkPhase *unit, int inNumSamples)
{
  float *quantum = IN(0);
  float *out = ZOUT(0);

  LOOP1(inNumSamples,
    ZXP(out) = 0.0f;
  )

  if (unit->sLink != nullptr)
  {
    const auto bufferDuration = std::chrono::microseconds(llround(
      unit->mRate->mSampleDur * static_cast<double>(inNumSamples) * 1e6));

    const auto bufferTimeOffset = std::chrono::microseconds(llround(
      unit->mWorld->mBufFramePos * unit->mRate->mSampleDur * 1e6));

    const auto timeAtBufferBegin =
      unit->sLink->clock().ticksToMicros(unit->mWorld->mAudioHostTime) + bufferTimeOffset;

    const auto timeAtBufferEnd = timeAtBufferBegin + bufferDuration;

    const auto timeInBuffer = timeAtBufferEnd - unit->mTime;

    const auto timePerSample = timeInBuffer / inNumSamples;

    auto timeline = unit->sLink->captureAudioTimeline();

    for (int i=0; i < inNumSamples; ++i)
    {
      unit->mOutBuf[0][i] =
        timeline.phaseAtTime(unit->mTime + i * timePerSample, *quantum) / *quantum;
    }

    unit->mTime = timeAtBufferEnd;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

PluginLoad(Link)
{
	ft = inTable;

  DefineDtorUnit(LinkTrig);
  DefineDtorUnit(LinkPhase);
}
