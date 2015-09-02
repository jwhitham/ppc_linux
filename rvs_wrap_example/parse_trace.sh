#!/bin/bash -e

echo 'converting trace format'
traceutils reader.flt trace.bin -o trace.rpz --ignore-warning W65003

echo 'parsing trace'
timeparser --clock-hz 800000000 --allow-incomplete -v trace.rpz ./rvs/out/results/project.rvd

echo 'making text report'
timexport --fmt text -o ./rvs/out/results/report.txt ./rvs/out/results/project.rvd

