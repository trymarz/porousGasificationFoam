#!/bin/bash

cd "cases" || {
  echo "cases does not exist"
  return
}

for case in */; do
  if [[ -f "$case/Allclean" ]]; then
    echo "Cleaning $case"
    (
      cd "$case" && ./Allclean
      echo "Case cleand"
    )
  else
    echo "⚠️ Allclean script not found - skipping $case"
    continue
  fi
done
