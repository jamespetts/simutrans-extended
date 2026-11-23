// etl-lines: fast line-level and RVA-level ETW sampled-CPU analyzer for a single game binary + PDB.
// Streams the raw .etl directly with ETWTraceEventSource (no .etlx conversion, no MS symbol downloads).
//
// Build (from repo root):
//   C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize+ /platform:x64 ^
//     /r:System.Net.Http.dll ^
//     /r:ai\tools\perf\Microsoft.Diagnostics.Tracing.TraceEvent.dll ^
//     /out:ai\tools\perf\etl-lines.exe scripts\perf\etl-lines.cs
//
// Usage:
//   ai\tools\perf\etl-lines.exe <trace.etl> <exeBaseName> <pdbPath> [funcSubstringFilter...]
//
// Requires TraceEvent's managed and native DLLs (amd64\msdia140.dll, etc.) beside the exe,
// matching the deployment layout of ai\tools\perf\etl-hotspots.exe.
using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.Diagnostics.Symbols;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Parsers;
using Microsoft.Diagnostics.Tracing.Parsers.Kernel;

class EtlLines
{
    static void Add(Dictionary<string, int> d, string k, int n)
    {
        int v;
        d[k] = d.TryGetValue(k, out v) ? v + n : n;
    }

    static List<KeyValuePair<string, int>> SortDesc(Dictionary<string, int> d)
    {
        var list = new List<KeyValuePair<string, int>>(d);
        list.Sort((a, b) => b.Value.CompareTo(a.Value));
        return list;
    }

    static int Main(string[] args)
    {
        if (args.Length < 3)
        {
            Console.WriteLine("usage: etl-lines <trace.etl> <exeBaseName> <pdbPath> [funcFilter...]");
            return 2;
        }
        string etlPath = args[0];
        string exeFilter = args[1];
        string pdbPath = args[2];
        var funcFilters = new List<string>();
        for (int i = 3; i < args.Length; i++) funcFilters.Add(args[i]);

        int targetPid = -1;
        ulong imageBase = 0;
        ulong imageEnd = 0;
        var rvaCounts = new Dictionary<uint, int>();
        var threadCounts = new Dictionary<int, int>();
        int totalSamplesAll = 0;
        int targetSamplesAll = 0;
        int targetSamplesInModule = 0;

        using (var source = new ETWTraceEventSource(etlPath))
        {
            var kernel = new KernelTraceEventParser(source);

            Action<ImageLoadTraceData> onImage = (img) =>
            {
                if (img.FileName != null && img.FileName.IndexOf(exeFilter, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    targetPid = img.ProcessID;
                    imageBase = img.ImageBase;
                    imageEnd = img.ImageBase + (ulong)(uint)img.ImageSize;
                }
            };
            kernel.ImageDCStart += onImage;
            kernel.ImageLoad += onImage;

            kernel.PerfInfoSample += (sp) =>
            {
                totalSamplesAll++;
                if (targetPid < 0)
                {
                    if (sp.ProcessName != null && sp.ProcessName.IndexOf(exeFilter, StringComparison.OrdinalIgnoreCase) >= 0)
                        targetPid = sp.ProcessID;
                    else
                        return;
                }
                if (sp.ProcessID != targetPid) return;
                targetSamplesAll++;
                int tc;
                threadCounts[sp.ThreadID] = threadCounts.TryGetValue(sp.ThreadID, out tc) ? tc + 1 : 1;

                ulong ip = sp.InstructionPointer;
                if (imageBase != 0 && ip >= imageBase && ip < imageEnd)
                {
                    targetSamplesInModule++;
                    uint rva = (uint)(ip - imageBase);
                    int c;
                    rvaCounts[rva] = rvaCounts.TryGetValue(rva, out c) ? c + 1 : 1;
                }
            };

            source.Process();
        }

        Console.WriteLine("Total ETW samples: {0:N0}; pid={1} samples={2:N0} (in-module={3:N0}, base=0x{4:X})",
            totalSamplesAll, targetPid, targetSamplesAll, targetSamplesInModule, imageBase);

        if (targetSamplesInModule == 0)
        {
            Console.WriteLine("No in-module samples found.");
            return 1;
        }

        var symReader = new SymbolReader(TextWriter.Null, Path.GetDirectoryName(pdbPath));
        NativeSymbolModule mod = symReader.OpenNativeSymbolFile(pdbPath);

        var funcTotals = new Dictionary<string, int>();
        var lineTotals = new Dictionary<string, int>();
        var filteredRvas = new List<KeyValuePair<string, int>>();

        foreach (var kv in rvaCounts)
        {
            uint rva = kv.Key;
            int count = kv.Value;
            uint symStart = 0;
            string fn = mod.FindNameForRva(rva, ref symStart);
            if (string.IsNullOrEmpty(fn)) fn = string.Format("rva_0x{0:X}", rva);
            Add(funcTotals, fn, count);

            bool match = funcFilters.Count == 0;
            if (!match)
            {
                foreach (var f in funcFilters)
                {
                    if (fn.IndexOf(f, StringComparison.OrdinalIgnoreCase) >= 0) { match = true; break; }
                }
            }
            if (match)
            {
                var loc = mod.SourceLocationForRva(rva);
                string file = (loc != null && loc.SourceFile != null) ? Path.GetFileName(loc.SourceFile.BuildTimeFilePath) : "?";
                int line = loc != null ? loc.LineNumber : 0;
                string key = string.Format("{0} | {1}:{2}", fn, file, line);
                Add(lineTotals, key, count);
                if (funcFilters.Count > 0)
                {
                    string rvaKey = string.Format("{0}+0x{1:X} (RVA 0x{2:X}) {3}:{4}", fn, rva - symStart, rva, file, line);
                    filteredRvas.Add(new KeyValuePair<string, int>(rvaKey, count));
                }
            }
        }

        Console.WriteLine("\n=== Threads (samples) ===");
        var tlist = new List<KeyValuePair<int, int>>(threadCounts);
        tlist.Sort((a, b) => b.Value.CompareTo(a.Value));
        foreach (var t in tlist)
            Console.WriteLine("  tid={0,-6} {1,7} ({2,5:F2}%)", t.Key, t.Value, 100.0 * t.Value / targetSamplesAll);

        Console.WriteLine("\n=== Top 30 Functions (self, % of all process samples) ===");
        int rank = 0;
        foreach (var kv in SortDesc(funcTotals))
        {
            if (++rank > 30) break;
            Console.WriteLine("  {0,6:F2}%  {1,7}  {2}", 100.0 * kv.Value / targetSamplesAll, kv.Value, kv.Key);
        }

        Console.WriteLine("\n=== Top 60 Source Lines (filtered, % of all process samples) ===");
        rank = 0;
        foreach (var kv in SortDesc(lineTotals))
        {
            if (++rank > 60) break;
            Console.WriteLine("  {0,6:F2}%  {1,7}  {2}", 100.0 * kv.Value / targetSamplesAll, kv.Value, kv.Key);
        }

        if (filteredRvas.Count > 0)
        {
            filteredRvas.Sort((a, b) => b.Value.CompareTo(a.Value));
            Console.WriteLine("\n=== Top 40 RVAs in filtered functions ===");
            rank = 0;
            foreach (var kv in filteredRvas)
            {
                if (++rank > 40) break;
                Console.WriteLine("  {0,6:F2}%  {1,7}  {2}", 100.0 * kv.Value / targetSamplesAll, kv.Value, kv.Key);
            }
        }

        return 0;
    }
}
