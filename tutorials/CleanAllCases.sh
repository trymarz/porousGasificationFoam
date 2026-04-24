#!/bin/bash

. ../utilities/bash_utils/helpers.sh

cd "cases" || {
  clog ERROR "cases does not exist"
  return
}

for case in */; do
  clog "Cleaning $case..."
  if [[ -f "$case/Allclean" ]]; then
    (
      cd "$case" && ./Allclean
      clog SUCCESS "Case cleaned"
    )
  else
    clog SKIP "Allclean script not found"
  fi
done

if [[ -d "simulation_logs/" ]]; then
  clog "Cleaning simulation_logs"
  rm -r simulation_logs/ && clog SUCCESS "simulation_logs/ removed"
fi

clog "CleanAllCases done!"
