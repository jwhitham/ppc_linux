#!/usr/bin/python
# Debugging tool to view source locations with vmlinux addresses

import pickle, sys, os


def bad():
   print ("Please provide a source file name.")
   sys.exit(1)

def main():
   if len(sys.argv) != 2:
      bad()

   file_name = sys.argv[1]

   if file_name.endswith(".dat"):
      file_name = file_name[:-4]

   data_file_name = file_name + ".dat"

   if not os.path.isfile(file_name):
      bad()

   if not os.path.isfile(data_file_name):
      symbol = dict()
   else:
      try:
         a = pickle.load(open(data_file_name, "rb"))
      except:
         bad()
      
      symbol = a["data"]

   number = 0
   for line in open(file_name, "rt"):
      number += 1
      left = ""
      v = symbol.get(number, None) 
      if v != None:
         left = "/* %08x */" % v
      print ('%-16s%s' % (left, line.rstrip()))
  
if __name__ == "__main__":
   main()

