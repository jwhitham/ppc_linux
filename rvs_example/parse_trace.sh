#!/bin/bash -e

echo 'converting trace format'
traceutils reader.flt trace.bin -o trace.txt

echo 'parsing trace'
timeparser trace.txt example.rvd

echo 'making text report'
timexport --fmt text -o report.txt example.rvd


