set -x
set +e

# This setting defines the clock in Hz - alter for your system
CLOCK_HZ=800000000

# This setting should be txt if debugging filters, then rpz when complete
FILTEXT=txt

# Run filters in the order specified in <rvslib>/filters/order.txt
FILTERID=1
FILTERFILE=$1
if [ -e "$$LIBDIR$$filters/order.txt" ]; then
  mkdir -p "$$INSTRDIR$$filtermods" 
  cat "$$LIBDIR$$filters/order.txt" | while read line
  do
    line="$(echo "$line" | cut -f1 -d"#")"   # only get text before the '#' comment character
    "$$RVSROOT$$bin/traceutils$$EXE$$" -f "$$LIBDIR$$filters/%%i" "$FILTERFILE" -o "$$INSTRDIR$$filtermods/filtered_$FILTERID.$FILTEXT"
    FILTERFILE="$$INSTRDIR$$filtermods/filtered_$FILTERID.$FILTEXT"
    FILTERID=$(expr $FILTERID + 1)
  done
fi

# Run demux filter if one exists as <rvslib>/filters/demux.flt
if [ -e "$$LIBDIR$$filters/demux.flt" ]; then
  mkdir -p "$$INSTRDIR$$filtermods"
  
  "$$RVSROOT$$bin/traceutils$$EXE$$" -f "$$LIBDIR$$filters/demux.flt" "$FILTERFILE" -o "$$INSTRDIR$$filtermods/demux.$FILTEXT" "$$RESULTDIR$$project.rvd"
  FILTERFILE="$$INSTRDIR$$filtermods/demux.$FILTEXT"
fi

# If a demux filter exists, invoke rapitask and rapitime
# If not, assume single threaded for rapitime only
if [ -e "$$LIBDIR$$filters/demux.flt" ]; then
  echo "Invoking RapiTask..."
  "$$RVSROOT$$bin/taskparser$$EXE$$" --clock-hz $CLOCK_HZ "$$INSTRDIR$$filtermods/demux_*.$FILTEXT" "$$RESULTDIR$$project.rvd" --overhead "$$INSTRDIR$$filtermods/demux_overhead.$FILTEXT"

  echo "Invoking RapiTime..."
  "$$RVSROOT$$bin/timeparser$$EXE$$" --clock-hz $CLOCK_HZ --allow-incomplete -v "$$INSTRDIR$$filtermods/demux_*.$FILTEXT" "$$RESULTDIR$$project.rvd"
else 
  echo "Invoking RapiTime..."
  "$$RVSROOT$$bin/timeparser$$EXE$$" --clock-hz $CLOCK_HZ --allow-incomplete -v "$FILTERFILE" "$$RESULTDIR$$project.rvd"
fi

echo "Completed timing generation for $$RESULTDIR$$project.rvd"
