/*
*----------------------------------------------------------------------
* Program:  dumpexts.exe
* Author:   Gordon Chaffee
*
* History:  The real functionality of this file was written by
*           Matt Pietrek in 1993 in his pedump utility.  I've
*           modified it to dump the externals in a bunch of object
*           files to create a .def file.
*
* Notes:    Visual C++ puts an underscore before each exported symbol.
*           This file removes them.  I don't know if this is a problem
*           this other compilers.  If VISUAL_CPLUSCPLUS is defined,
*           the underscore is removed.  If not, it isn't.  To get a
*           full dump of an object file, use the -f option.  This can
*           help determine the something that may be different with a
*           compiler other than Visual C++.
*   ======================================
* Corrections (Axel 2006-04-04):
*   Conversion to C++. Mostly.
*
*   ======================================
* Corrections (Valery Fine 23/02/98):
*
*           The "(vector) deleting destructor" MUST not be exported
*           To recognize it the following test are introduced:
*  "@@UAEPAXI@Z"  scalar deleting dtor
*  "@@QAEPAXI@Z"  vector deleting dtor
*  "AEPAXI@Z"     vector deleting dtor with thunk adjustor
*   ======================================
* Corrections (Valery Fine 12/02/97):
*
*    It created a wrong EXPORTS for the global pointers and constants.
*    The Section Header has been involved to discover the missing information
*    Now the pointers are correctly supplied  supplied with "DATA" descriptor
*        the constants  with no extra descriptor.
*
* Corrections (Valery Fine 16/09/96):
*
*     It didn't work for C++ code with global variables and class definitons
*     The DumpExternalObject function has been introduced to generate .DEF file
*
* Author:   Valery Fine 16/09/96  (E-mail: fine@vxcern.cern.ch)
*----------------------------------------------------------------------
*/

static char sccsid[] = "@(#) winDumpExts.c 1.2 95/10/03 15:27:34";

#include <windows.h>
#include <stdio.h>
#include <string>
#include <fstream>

/*
*----------------------------------------------------------------------
* GetArgcArgv --
*
*      Break up a line into argc argv
*----------------------------------------------------------------------
*/
int
GetArgcArgv(std::string &s, char **argv)
{
   int quote = 0;
   int argc = 0;
   std::string::iterator bp = s.begin();

   while (1) {
      while (isspace(*bp)) {
         bp++;
      }
      if (*bp == '\n' || *bp == '\0') {
         *bp = '\0';
         return argc;
      }
      if (*bp == '\"') {
         quote = 1;
         bp++;
      }
      argv[argc++] = &(*bp);

      while (*bp != '\0') {
         if (quote) {
            if (*bp == '\"') {
               quote = 0;
               *bp = '\0';
               bp++;
               break;
            }
            bp++;
            continue;
         }
         if (isspace(*bp)) {
            *bp = '\0';
            bp++;
            break;
         }
         bp++;
      }
   }
}

/*
*  The names of the first group of possible symbol table storage classes
*/
char * SzStorageClass1[] = {
   "NULL","AUTOMATIC","EXTERNAL","STATIC","REGISTER","EXTERNAL_DEF","LABEL",
   "UNDEFINED_LABEL","MEMBER_OF_STRUCT","ARGUMENT","STRUCT_TAG",
   "MEMBER_OF_UNION","UNION_TAG","TYPE_DEFINITION","UNDEFINED_STATIC",
   "ENUM_TAG","MEMBER_OF_ENUM","REGISTER_PARAM","BIT_FIELD"
};

/*
* The names of the second group of possible symbol table storage classes
*/
char * SzStorageClass2[] = {
   "BLOCK","FUNCTION","END_OF_STRUCT","FILE","SECTION","WEAK_EXTERNAL"
};

/*
*----------------------------------------------------------------------
* GetSZStorageClass --
*
*      Given a symbol storage class value, return a descriptive
*      ASCII string
*----------------------------------------------------------------------
*/
PSTR
GetSZStorageClass(BYTE storageClass)
{
   if ( storageClass <= IMAGE_SYM_CLASS_BIT_FIELD )
      return SzStorageClass1[storageClass];
   else if ( (storageClass >= IMAGE_SYM_CLASS_BLOCK)
      && (storageClass <= IMAGE_SYM_CLASS_WEAK_EXTERNAL) )
      return SzStorageClass2[storageClass-IMAGE_SYM_CLASS_BLOCK];
   else
      return "???";
}

void AddHex(std::string& buf, long val, bool caps=false)
{
   buf += "0x";
   int len=buf.length();
   while (val) {
      char hex = val & 16;
      val = val >> 4;
      if (hex<10) hex+='0';
      else if (caps) hex+='A'-10;
      else hex+='a'-10;
      buf.insert(len, hex, 1);
   }
}

/*
*----------------------------------------------------------------------
* GetSectionName --
*
*      Used by DumpSymbolTable, it gives meaningful names to
*      the non-normal section number.
*
* Results:
*      A name is returned in buffer
*----------------------------------------------------------------------
*/
void
GetSectionName(PIMAGE_SYMBOL pSymbolTable, std::string& buffer)
{
   DWORD section;

   section = pSymbolTable->SectionNumber;

   switch ( (SHORT)section )
   {
   case IMAGE_SYM_UNDEFINED: if (pSymbolTable->Value) buffer += "COMM"; else buffer += "UNDEF"; break;
   case IMAGE_SYM_ABSOLUTE:  buffer += "ABS  "; break;
   case IMAGE_SYM_DEBUG:     buffer += "DEBUG"; break;
   default: AddHex(buffer, section, true);
   }
}

/*
*----------------------------------------------------------------------
* GetSectionCharacteristics --
*
*      Converts the Characteristics field of IMAGE_SECTION_HEADER
*      to print.
*
*  Results:
*       A definiton of the section symbol type
*----------------------------------------------------------------------
*/
void
GetSectionCharacteristics(PIMAGE_SECTION_HEADER pSectionHeaders, int nSectNum, std::string &buffer)
{
   DWORD SectChar;
   std::string TempBuf;
   buffer.clear();
   if (nSectNum > 0) {
      SectChar = pSectionHeaders[nSectNum-1].Characteristics;

      buffer = " ";
      AddHex(buffer, SectChar);
      if       (SectChar & IMAGE_SCN_CNT_CODE)                buffer += " Code";
      else if  (SectChar & IMAGE_SCN_CNT_INITIALIZED_DATA)    buffer += " Init. data";
      else if  (SectChar & IMAGE_SCN_CNT_UNINITIALIZED_DATA ) buffer += " UnInit data";
      else                                                    buffer += " Unknow type";

      if   (SectChar & IMAGE_SCN_MEM_READ)  {
         buffer += " Read";
         if (SectChar & IMAGE_SCN_MEM_WRITE)
            buffer += " and Write";
         else buffer += " only";
      }
      else if (SectChar & IMAGE_SCN_MEM_WRITE)
         buffer +=" Write only";

   }
}

/*
*----------------------------------------------------------------------
* DumpSymbolTable --
*
*      Dumps a COFF symbol table from an EXE or OBJ.  We only use
*      it to dump tables from OBJs.
*----------------------------------------------------------------------
*/
void
DumpSymbolTable(PIMAGE_SYMBOL pSymbolTable, PIMAGE_SECTION_HEADER pSectionHeaders, FILE *fout, unsigned cSymbols)
{
   unsigned i;
   PSTR stringTable;
   std::string sectionName;
   std::string sectionCharacter;
   int iSectNum;

   fprintf(fout, "Symbol Table - %X entries  (* = auxillary symbol)\n",
      cSymbols);

   fprintf(fout,
      "Indx Name                 Value    Section    cAux  Type    Storage  Character\n"
      "---- -------------------- -------- ---------- ----- ------- -------- ---------\n");

   /*
   * The string table apparently starts right after the symbol table
   */
   stringTable = (PSTR)&pSymbolTable[cSymbols];

   for ( i=0; i < cSymbols; i++ ) {
      fprintf(fout, "%04X ", i);
      if ( pSymbolTable->N.Name.Short != 0 )
         fprintf(fout, "%-20.8s", pSymbolTable->N.ShortName);
      else
         fprintf(fout, "%-20s", stringTable + pSymbolTable->N.Name.Long);

      fprintf(fout, " %08X", pSymbolTable->Value);

      iSectNum = pSymbolTable->SectionNumber;
      GetSectionName(pSymbolTable, sectionName);
      fprintf(fout, " sect:%s aux:%X type:%02X st:%s",
         sectionName.c_str(),
         pSymbolTable->NumberOfAuxSymbols,
         pSymbolTable->Type,
         GetSZStorageClass(pSymbolTable->StorageClass) );

      GetSectionCharacteristics(pSectionHeaders,iSectNum,sectionCharacter);
      fprintf(fout," hc: %s \n",sectionCharacter.c_str());
#if 0
      if ( pSymbolTable->NumberOfAuxSymbols )
         DumpAuxSymbols(pSymbolTable);
#endif

      /*
      * Take into account any aux symbols
      */
      i += pSymbolTable->NumberOfAuxSymbols;
      pSymbolTable += pSymbolTable->NumberOfAuxSymbols;
      pSymbolTable++;
   }
}

/*
*----------------------------------------------------------------------
* DumpExternals --
*
*      Dumps a COFF symbol table from an EXE or OBJ.  We only use
*      it to dump tables from OBJs.
*----------------------------------------------------------------------
*/
void
DumpExternals(PIMAGE_SYMBOL pSymbolTable, FILE *fout, unsigned cSymbols)
{
   unsigned i;
   PSTR stringTable;
   std::string symbol;

   /*
   * The string table apparently starts right after the symbol table
   */
   stringTable = (PSTR)&pSymbolTable[cSymbols];

   for ( i=0; i < cSymbols; i++ ) {
      if (pSymbolTable->SectionNumber > 0 && pSymbolTable->Type == 0x20) {
         if (pSymbolTable->StorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
            if (pSymbolTable->N.Name.Short != 0) {
               symbol = "";
               symbol.insert(0, (const char *)(pSymbolTable->N.ShortName), 8);
            } else {
               symbol = stringTable + pSymbolTable->N.Name.Long;
            }
            std::string::size_type posAt = symbol.find('@');
            if (posAt != std::string::npos) symbol.erase(posAt);
#ifndef VISUAL_CPLUSPLUS
            fprintf(fout, "\t%s\n", symbol.c_str());
#else
            fprintf(fout, "\t%s\n", symbol.c_str()+1);
#endif
         }
      }

      /*
      * Take into account any aux symbols
      */
      i += pSymbolTable->NumberOfAuxSymbols;
      pSymbolTable += pSymbolTable->NumberOfAuxSymbols;
      pSymbolTable++;
   }
}

/*
*----------------------------------------------------------------------
* DumpExternalsObjects --
*
*      Dumps a COFF symbol table from an EXE or OBJ.  We only use
*      it to dump tables from OBJs.
*----------------------------------------------------------------------
*/
void
DumpExternalsObjects(PIMAGE_SYMBOL pSymbolTable, PIMAGE_SECTION_HEADER pSectionHeaders,
                     FILE *fout, unsigned cSymbols, int fort)
{
   unsigned i;
   PSTR stringTable;
   char *f;
   std::string symbol;
   DWORD SectChar;
   static int fImportFlag = -1;  /*  The status is nor defined yet */

   /*
   * The string table apparently starts right after the symbol table
   */
   stringTable = (PSTR)&pSymbolTable[cSymbols];

   for ( i=0; i < cSymbols; i++ ) {
      if (pSymbolTable->SectionNumber > 0 && ( pSymbolTable->Type == 0x20 || pSymbolTable->Type == 0x0)) {
         if (pSymbolTable->StorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
            /*
            *    The name of the Function entry points
            */
            if (pSymbolTable->N.Name.Short != 0) {
               symbol = "";
               symbol.insert(0, (const char *)pSymbolTable->N.ShortName, 8);
            } else {
               symbol = stringTable + pSymbolTable->N.Name.Long;
            }

            while (isspace(symbol[0])) symbol.erase(0,1);
#ifdef VISUAL_CPLUSPLUS
            if (symbol[0] == '_') symbol.erase(0,1);
            if (fort) {
               std::string::size_type posAt = symbol.find('@');
               if (posAt != std::string::npos) symbol.erase(posAt);
            }
#endif
            if (fImportFlag) {
               fImportFlag = 0;
               fprintf(fout,"EXPORTS \n");
            }
            /*
            Check whether it is "Scalar deleting destructor" and
            "Vector deleting destructor"
            */
            /*
            if (!strstr(s,"@@UAEPAXI@Z") && !strstr(s,"@@QAEPAXI@Z") &&
            !strstr(s,"@AEPAXI@Z")   && !strstr(s,"AEPAXI@Z")    &&
            !strstr(s,"real@"))
            */
            const char *scalarPrefix = "??_G";
            const char *vectorPrefix = "??_E";
            if (symbol.compare(0, 4, scalarPrefix) &&
               symbol.compare(0, 4, vectorPrefix) &&
               symbol.find("real@") == std::string::npos)
            {
               SectChar = pSectionHeaders[pSymbolTable->SectionNumber-1].Characteristics;
               if (!pSymbolTable->Type  && (SectChar & IMAGE_SCN_MEM_WRITE)) {
                  // Read only (i.e. constants) must be excluded
                  fprintf(fout, "\t%s \t DATA\n", symbol.c_str());
               } else {
                  if ( pSymbolTable->Type  || !(SectChar & IMAGE_SCN_MEM_READ)) {
                     fprintf(fout, "\t%s\n", symbol.c_str());
                  } else {
                     //                    printf(" strange symbol: %s \n",s);
                  }
               }
            }
         }
      }
      else if (pSymbolTable->SectionNumber == IMAGE_SYM_UNDEFINED && !pSymbolTable->Type && 0){
         /*
         *    The IMPORT global variable entry points
         */
         if (pSymbolTable->StorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
            symbol = stringTable + pSymbolTable->N.Name.Long;
            while (isspace(symbol[0]))  symbol.erase(0,1);
            if (symbol[0] == '_') symbol.erase(0,1);
            if (!fImportFlag) {
               fImportFlag = 1;
               fprintf(fout,"IMPORTS \n");
            }
            fprintf(fout, "\t%s DATA \n", symbol.c_str()+1);
         }
      }

      /*
      * Take into account any aux symbols
      */
      i += pSymbolTable->NumberOfAuxSymbols;
      pSymbolTable += pSymbolTable->NumberOfAuxSymbols;
      pSymbolTable++;
   }
}

/*
*----------------------------------------------------------------------
* DumpObjFile --
*
*      Dump an object file--either a full listing or just the exported
*      symbols.
*----------------------------------------------------------------------
*/
void
DumpObjFile(PIMAGE_FILE_HEADER pImageFileHeader, FILE *fout, int full, int fort)
{
   PIMAGE_SYMBOL PCOFFSymbolTable;
   PIMAGE_SECTION_HEADER PCOFFSectionHeaders;
   DWORD COFFSymbolCount;

   PCOFFSymbolTable = (PIMAGE_SYMBOL)
      ((DWORD)pImageFileHeader + pImageFileHeader->PointerToSymbolTable);
   COFFSymbolCount = pImageFileHeader->NumberOfSymbols;

   PCOFFSectionHeaders = (PIMAGE_SECTION_HEADER)
      ((DWORD)pImageFileHeader          +
      IMAGE_SIZEOF_FILE_HEADER +
      pImageFileHeader->SizeOfOptionalHeader);


   if (full) {
      DumpSymbolTable(PCOFFSymbolTable, PCOFFSectionHeaders, fout, COFFSymbolCount);
   } else {
      /*      DumpExternals(PCOFFSymbolTable, fout, COFFSymbolCount); */
      DumpExternalsObjects(PCOFFSymbolTable, PCOFFSectionHeaders, fout, COFFSymbolCount, fort);
   }
}

/*
*----------------------------------------------------------------------
* DumpFile --
*
*      Open up a file, memory map it, and call the appropriate
*      dumping routine
*----------------------------------------------------------------------
*/
void
DumpFile(LPSTR filename, FILE *fout, int full, int fort)
{
   HANDLE hFile;
   HANDLE hFileMapping;
   LPVOID lpFileBase;
   PIMAGE_DOS_HEADER dosHeader;

   hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

   if (hFile == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "Couldn't open file with CreateFile()\n");
      return;
   }

   hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
   if (hFileMapping == 0) {
      CloseHandle(hFile);
      fprintf(stderr, "Couldn't open file mapping with CreateFileMapping()\n");
      return;
   }

   lpFileBase = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
   if (lpFileBase == 0) {
      CloseHandle(hFileMapping);
      CloseHandle(hFile);
      fprintf(stderr, "Couldn't map view of file with MapViewOfFile()\n");
      return;
   }

   dosHeader = (PIMAGE_DOS_HEADER)lpFileBase;
   if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
#if 0
      DumpExeFile( dosHeader );
#else
      fprintf(stderr, "File is an executable.  I don't dump those.\n");
      return;
#endif
   }
   /* Does it look like a i386 COFF OBJ file??? */
   else if ((dosHeader->e_magic == 0x014C) && (dosHeader->e_sp == 0)) {
      /*
      * The two tests above aren't what they look like.  They're
      * really checking for IMAGE_FILE_HEADER.Machine == i386 (0x14C)
      * and IMAGE_FILE_HEADER.SizeOfOptionalHeader == 0;
      */
      DumpObjFile((PIMAGE_FILE_HEADER) lpFileBase, fout, full, fort);
   } else {
      printf("unrecognized file format\n");
   }
   UnmapViewOfFile(lpFileBase);
   CloseHandle(hFileMapping);
   CloseHandle(hFile);
}

void
main(int argc, char **argv)
{
   std::string cmdline;
   int i, arg;
   FILE *fout;
   int pos;
   int full = 0;
   int fort = 0;
   char *dllname = "";
   char *outfile = NULL;

   if (argc < 3) {
Usage:
      fprintf(stderr, "Usage: %s ?-o outfile? ?-f(ull)? <dllname> <object filenames> ..\n", argv[0]);
      exit(1);
   }

   arg = 1;
   while (argv[arg][0] == '-') {
      if (strcmp(argv[arg], "--") == 0) {
         arg++;
         break;
      } else if (strcmp(argv[arg], "-f") == 0) {
         full = 1;
      } else if (strcmp(argv[arg], "-x") == 0) {
         fort = 1;
      } else if (strcmp(argv[arg], "-o") == 0) {
         arg++;
         if (arg == argc) {
            goto Usage;
         }
         outfile = argv[arg];
      }
      arg++;
   }
   if (arg == argc) {
      goto Usage;
   }

   if (outfile) {
      fout = fopen(outfile, "w+");
      if (fout == NULL) {
         fprintf(stderr, "Unable to open \'%s\' for writing:\n",
            argv[arg]);
         perror("");
         exit(1);
      }
   } else {
      fout = stdout;
   }

   if (! full) {
      dllname = argv[arg];
      arg++;
      if (arg == argc) {
         goto Usage;
      }
      fprintf(fout, "LIBRARY    %s\n", dllname);
#ifndef _X86_
      fprintf(fout, "CODE PRELOAD MOVEABLE DISCARDABLE\n");
      fprintf(fout, "DATA PRELOAD MOVEABLE MULTIPLE\n\n");
#endif
   }

   for (; arg < argc; arg++) {
      WIN32_FIND_DATA FindFileData;
      HANDLE SearchFile;
      if (argv[arg][0] == '@') {
         std::ifstream fargs(&argv[arg][1]);
         if (!fargs) {
            fprintf(stderr, "Unable to open \'%s\' for reading:\n",
               argv[arg]);
            perror("");
            exit(1);
         }
         char *fargv[1000];
         for (i = 0; i < arg; i++) {
            cmdline += argv[i];
            fargv[i] = argv[i];
         }
         std::string line;
         std::getline(fargs, line);
         cmdline += line;
         fprintf(stderr, "%s\n", line.c_str());
         i += GetArgcArgv(line, &fargv[i]);
         argc = i;
         argv = fargv;
      }
      /*
      *  Argument can contain the wildcard names
      */
      SearchFile = FindFirstFile(argv[arg],&FindFileData);
      if (SearchFile == INVALID_HANDLE_VALUE){
         fprintf(stderr, "Unable to find \'%s\' for reading:\n",
            argv[arg]);
         exit(1);
      }
      else  {
         /*
         *  Since WIN32_FIND_DATA has no path information one has to extract it himself.
         */
         TCHAR *filename = argv[arg];
         TCHAR path[2048];
         int i = strlen(filename);
         i--;
         while( filename[i] != '\\' && filename[i] != '/'  && i >=0) i--;
         do
         {
            if (i >= 0) strncpy( path, filename, i+1); /* Generate the 'path' info */
            path[i+1] = '\0';
            DumpFile(strcat(path, FindFileData.cFileName), fout, full, fort);
         } while (FindNextFile(SearchFile,&FindFileData));


         FindClose(SearchFile);
      }
   }
   exit(0);
}
