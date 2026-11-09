# scripts/guard/video.gen through genwav --video, with
# scripts/guard/videoramp.dsp as the video synth at 32x24@30: a gray ramp
# while the one note, 1-1.5 s, sounds.
#
# The frames are read back from the .y4m: black before the note and after
# it, the ramp across a frame while it sounds, as many frames as the WAV
# is long, the same bytes on a second render, and --from and --length
# trimming the two files to the same whole frames. And the audio reaching
# the video synth: the ramp swapped for each of the bridge's chanargs in
# turn, which light the frames under the note, or for @in_bass, which a
# C4 organ leaves dark.
if(NOT GENWAV OR NOT PIECE OR NOT VIDEO_DSP OR NOT PLUGIN_DIR OR NOT DSP_PATH OR NOT WORK)
  message(FATAL_ERROR "GENWAV, PIECE, VIDEO_DSP, PLUGIN_DIR, DSP_PATH and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{THINK_DSP_PATH} "${DSP_PATH}")

set(W 32)
set(H 24)
set(FPS 30)
math(EXPR FRAME "6 + 3 * ${W} * ${H}")

set(dsp "${VIDEO_DSP}")

function(render name)
  execute_process(
      COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 4 -q -o "${WORK}/${name}.wav"
              --video "${dsp}" --video-out "${WORK}/${name}.y4m"
              --raster ${W}x${H}@${FPS} ${ARGN} "${PIECE}"
      ERROR_VARIABLE report
      RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "genwav exited ${rc} on ${name}: ${report}")
  endif()
endfunction()

# The header's length, the frame count, and the WAV's length in frames of
# video, rounded up: a frame is written for any audio it holds.
function(measure name)
  file(READ "${WORK}/${name}.y4m" head LIMIT 128)
  string(FIND "${head}" "\n" eol)
  math(EXPR start "${eol} + 1")
  file(SIZE "${WORK}/${name}.y4m" size)
  math(EXPR frames "(${size} - ${start}) / ${FRAME}")
  math(EXPR rest "(${size} - ${start}) % ${FRAME}")
  if(NOT rest EQUAL 0)
    message(FATAL_ERROR "${name}.y4m ends ${rest} bytes into a frame")
  endif()
  file(SIZE "${WORK}/${name}.wav" wav)
  math(EXPR samples "(${wav} - 44) / 4")
  math(EXPR want "(${samples} * ${FPS} + 44099) / 44100")
  set(start ${start} PARENT_SCOPE)
  set(frames ${frames} PARENT_SCOPE)
  set(samples ${samples} PARENT_SCOPE)
  set(want ${want} PARENT_SCOPE)
endfunction()

# Y at column `x' of the first row of frame `f'.
function(luma name f x out)
  math(EXPR at "${start} + ${f} * ${FRAME} + 6 + ${x}")
  file(READ "${WORK}/${name}.y4m" byte OFFSET ${at} LIMIT 1 HEX)
  math(EXPR y "0x${byte}")
  set(${out} ${y} PARENT_SCOPE)
endfunction()

render(whole)
measure(whole)
if(NOT frames EQUAL want)
  message(FATAL_ERROR "${frames} frames for ${samples} samples of audio; want ${want}")
endif()

# A frame ends a video window or so after the audio it shows, and the note
# lands within an audio window of its time: a frame's grace either side.
foreach(f 0 15 28 47 55)
  luma(whole ${f} 31 y)
  if(NOT y EQUAL 16)
    message(FATAL_ERROR "frame ${f}, with no note, is not black: Y ${y}")
  endif()
endforeach()
foreach(f 32 37 42)
  set(ramp "")
  foreach(x 0 8 31)
    luma(whole ${f} ${x} y)
    string(APPEND ramp " ${y}")
  endforeach()
  # 16 + 219 x: black at x = 0, a quarter at 8/32, 31/32 near white.
  if(NOT ramp STREQUAL " 16 71 228")
    message(FATAL_ERROR "frame ${f}, while the note sounds, is not the ramp:${ramp}")
  endif()
endforeach()

render(again)
file(SHA256 "${WORK}/whole.y4m" first)
file(SHA256 "${WORK}/again.y4m" second)
if(NOT first STREQUAL second)
  message(FATAL_ERROR "two renders of the same piece wrote different frames")
endif()

render(trimmed --from 1 --length 0.5)
measure(trimmed)
if(NOT frames EQUAL 15 OR NOT samples EQUAL 22050)
  message(FATAL_ERROR "--from 1 --length 0.5 kept ${frames} frames and ${samples} samples; want 15 and 22050")
endif()
luma(trimmed 5 31 y)
if(NOT y EQUAL 228)
  message(FATAL_ERROR "the trimmed video does not start where the note does: Y ${y} at frame 5")
endif()

# A raster so small that a window holds more than a frame: still a frame
# for each 1/FPS of audio, and none past it.
set(FPS 200)
math(EXPR FRAME "6 + 3 * 4 * 1")
render(tiny --raster 4x1@200)
measure(tiny)
if(NOT frames EQUAL want)
  message(FATAL_ERROR "4x1@200: ${frames} frames for ${samples} samples of audio; want ${want}")
endif()

# The bridge: a gray as bright as each chanarg, scaled so the organ's C4
# reads well clear of black.
set(FPS 30)
math(EXPR FRAME "6 + 3 * ${W} * ${H}")
file(READ "${VIDEO_DSP}" ramp)
foreach(arg in_level in_level0 in_mid in_bass knob_k)
  string(REPLACE "in0 = s->x;" "in0 = @${arg} * 8;" text "${ramp}")
  file(WRITE "${WORK}/${arg}.dsp" "@${arg} = 0;\n${text}")
  set(dsp "${WORK}/${arg}.dsp")
  render(${arg})
  measure(${arg})
  luma(${arg} 37 0 y)
  if(arg STREQUAL "in_bass")
    # A note's onset and the window's skirt reach below 250 Hz a little.
    math(EXPR bass "${y} - 16")
    math(EXPR quarter "(${mid_y} - 16) / 4")
    if(NOT bass LESS quarter)
      message(FATAL_ERROR "@in_bass reads a C4 organ as bass: Y ${y} under the note, @in_mid ${mid_y}")
    endif()
  elseif(NOT y GREATER 40)
    message(FATAL_ERROR "@${arg} does not reach the video synth: Y ${y} under the note")
  endif()
  if(arg STREQUAL "in_mid")
    set(mid_y ${y})
  endif()
endforeach()
