// pdb-sizes: inspect exact C++ struct/class sizes (and optional symbol presence) from a compiled PDB.
// Uses DIA via TraceEvent's SymbolReader + Dia2Lib, avoiding temporary sizeof probes in game code.
//
// Build (from repo root):
//   C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize+ /platform:x64 ^
//     /r:System.Net.Http.dll ^
//     /r:ai\tools\perf\Microsoft.Diagnostics.Tracing.TraceEvent.dll ^
//     /r:ai\tools\perf\Dia2Lib.dll ^
//     /out:ai\tools\perf\pdb-sizes.exe scripts\perf\pdb-sizes.cs
//
// Usage:
//   ai\tools\perf\pdb-sizes.exe <path.pdb> <typeName1> [typeName2 ...]
using System;
using System.IO;
using System.Reflection;
using Dia2Lib;
using Microsoft.Diagnostics.Symbols;

class PdbSizes
{
    static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.WriteLine("usage: pdb-sizes <path.pdb> <typeName1> [typeName2 ...]");
            return 2;
        }
        string pdbPath = args[0];
        var sr = new SymbolReader(TextWriter.Null, Path.GetDirectoryName(pdbPath));
        var mod = sr.OpenNativeSymbolFile(pdbPath);
        var field = mod.GetType().GetField("m_session", BindingFlags.NonPublic | BindingFlags.Instance);
        if (field == null)
        {
            Console.WriteLine("ERROR: could not access DIA session on NativeSymbolModule");
            return 1;
        }
        var sess = (IDiaSession)field.GetValue(mod);
        for (int i = 1; i < args.Length; i++)
        {
            string name = args[i];
            IDiaEnumSymbols en;
            sess.globalScope.findChildren(SymTagEnum.SymTagUDT, name, 0, out en);
            IDiaSymbol sym;
            uint fetched;
            en.Next(1, out sym, out fetched);
            if (fetched == 1)
                Console.WriteLine("{0,-28} = {1} bytes", name, sym.length);
            else
                Console.WriteLine("{0,-28} = <not found>", name);
        }
        return 0;
    }
}
