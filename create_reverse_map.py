#!/usr/bin/python
# Debugging tool to associate source locations with vmlinux addresses

import subprocess, pickle

VERSION = 3

def load_file(name):
   try:
      a = pickle.load(open(name, "rb"))
      if a["version"] != VERSION:
         a = None
   except:
      a = None

   if a != None:
      return a["data"]
   else:
      return dict()

def save_file(name, symbols):
   a = {
      "version" : VERSION,
      "data" : symbols,
      }
   pickle.dump(a, open(name, "wb"))
   

def main():
   p = subprocess.Popen(["../linux-tools/bin/powerpc-eabi-objdump", "-dl", "vmlinux"],
         stdout = subprocess.PIPE)
   symbols = dict()
   master = dict()
   address = 0

   i = 0
   for line in p.stdout:
      if len(line) <= 10:
         pass
      elif line[8] == ':' and line[9] == '\t':
         # Address and instruction
         try:
            address = int(line[:8], 16)
         except:
            pass
     
      elif line[0] == '/':
         # File name and path
         fields = line.strip().split(":")
         if len(fields) == 2:
            file_name = fields[0]
            try:
               line_number = int(fields[1])
            except:
               line_number = 0

            if line_number > 0:
               if not file_name in master:
                  print ("1: " + file_name)
                  master[file_name] = load_file(file_name)

               symbols = master[file_name]

               if not line_number in symbols:
                  symbols[line_number] = address

   for (file_name, symbols) in master.iteritems():
      print ("2: " + file_name)
      save_file(file_name + ".dat", symbols)

   p.kill()

if __name__ == "__main__":
   main()

