# smx_apple_team(<out>): the Apple developer team of a valid signing identity
# in the keychain, preferring a Developer ID certificate, which notarization
# needs, to an Apple Development one. Empty when there is neither. The team is
# the certificate subject's OU; the ID in parentheses after an Apple
# Development certificate's name is the developer's own, not the team's.
function(smx_apple_team out)
  set(team "")
  execute_process(
    COMMAND security find-identity -v -p codesigning
    OUTPUT_VARIABLE identities
    ERROR_QUIET
  )
  foreach(kind "Developer ID Application" "Apple Development")
    string(REGEX MATCHALL "\"${kind}: [^\"]+\"" names "${identities}")
    foreach(name IN LISTS names)
      string(REPLACE "\"" "" name "${name}")
      execute_process(
        COMMAND security find-certificate -c "${name}" -p
        COMMAND openssl x509 -noout -subject
        OUTPUT_VARIABLE subject
        ERROR_QUIET
      )
      if(subject MATCHES "OU ?= ?([A-Z0-9]+)")
        set(team "${CMAKE_MATCH_1}")
        break()
      endif()
    endforeach()
    if(team)
      break()
    endif()
  endforeach()
  set(${out} "${team}" PARENT_SCOPE)
endfunction()
