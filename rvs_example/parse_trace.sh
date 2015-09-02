#!/bin/bash -e

echo 'converting trace format'
traceutils reader.flt trace.bin -o trace.rpz --ignore-warning W65003

echo 'parsing trace'
timeparser trace.rpz example.rvd

echo 'making text report'
timexport --fmt text -o report.txt example.rvd


