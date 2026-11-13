# Is every call to setenv() or unsetenv() on the POSIX side of a _WIN32 test?
#
# Neither exists on MinGW's UCRT, so a bare call builds everywhere but
# Windows, and the first anyone hears of it is the Windows job. The tree's
# rule is Glib::setenv where glibmm is linked anyway, and otherwise the POSIX
# call under `#ifndef _WIN32' (or after the `#else' of an `#ifdef _WIN32')
# with _putenv_s on the other side. This finds a call that is neither.
#
# A file comparison, so it needs no build and runs wherever ctest does.
#
#   cmake -DTHINK_SOURCE_DIR=<tree> -P cmake/CheckPosixEnv.cmake

if(NOT DEFINED THINK_SOURCE_DIR)
  message(FATAL_ERROR "THINK_SOURCE_DIR is not set")
endif()

set(found "")

foreach(dir libthink plugins scripts src wasm hid camera game video plugin)
  file(GLOB_RECURSE sources
       "${THINK_SOURCE_DIR}/${dir}/*.cpp"
       "${THINK_SOURCE_DIR}/${dir}/*.c"
       "${THINK_SOURCE_DIR}/${dir}/*.h")

  foreach(path IN LISTS sources)
    file(READ "${path}" content)

    # A line at a time, as list items: the characters a CMake list gives
    # meaning to go first.
    string(REPLACE ";" "," content "${content}")
    string(REPLACE "[" "(" content "${content}")
    string(REPLACE "]" ")" content "${content}")
    string(REPLACE "\\" "/" content "${content}")
    string(REPLACE "\n" ";" lines "${content}")

    # What each enclosing #if says about Windows: win, posix or other.
    set(stack "")
    set(number 0)

    foreach(line IN LISTS lines)
      math(EXPR number "${number} + 1")

      if(line MATCHES "^[ \t]*#[ \t]*(ifdef[ \t]+_WIN32|if[ \t]+defined[ \t]*\\(_WIN32\\))")
        list(APPEND stack win)
      elseif(line MATCHES "^[ \t]*#[ \t]*(ifndef[ \t]+_WIN32|if[ \t]+!defined[ \t]*\\(_WIN32\\))")
        list(APPEND stack posix)
      elseif(line MATCHES "^[ \t]*#[ \t]*if")
        list(APPEND stack other)
      elseif(line MATCHES "^[ \t]*#[ \t]*(else|elif)")
        list(LENGTH stack depth)
        if(depth GREATER 0)
          list(POP_BACK stack top)
          if(top STREQUAL "win" AND line MATCHES "#[ \t]*else")
            list(APPEND stack posix)
          elseif(top STREQUAL "posix" AND line MATCHES "#[ \t]*else")
            list(APPEND stack win)
          else()
            list(APPEND stack other)
          endif()
        endif()
      elseif(line MATCHES "^[ \t]*#[ \t]*endif")
        list(LENGTH stack depth)
        if(depth GREATER 0)
          list(POP_BACK stack)
        endif()
      elseif(line MATCHES "(^|[^:A-Za-z0-9_])(un)?setenv[ \t]*\\(")
        list(FIND stack posix posix)
        if(posix EQUAL -1)
          file(RELATIVE_PATH where "${THINK_SOURCE_DIR}" "${path}")
          list(APPEND found "${where}:${number}")
        endif()
      endif()
    endforeach()
  endforeach()
endforeach()

if(found)
  list(JOIN found "\n  " lines)
  message(FATAL_ERROR
    "setenv or unsetenv outside the POSIX side of a _WIN32 test, which "
    "MinGW's UCRT does not have -- use Glib::setenv where glibmm is "
    "linked, or put the call under #ifndef _WIN32 with _putenv_s for "
    "Windows:\n  ${lines}")
endif()

message(STATUS "every setenv and unsetenv is on the POSIX side of a _WIN32 test")
