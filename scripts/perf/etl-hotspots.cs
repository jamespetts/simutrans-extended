// etl-hotspots: automated CPU-sampling hotspot extraction from a WPR/xperf ETL.
// Usage: etl-hotspots.exe <trace.etl|trace.etlx> <processNameFilter> <outPrefix> [topN]
// Outputs: <outPrefix>-self.csv (leaf/self time), <outPrefix>-incl.csv (inclusive), summary on stdout.
//   topN (optional): if > 0, limit each CSV to the top N rows.
// Requires _NT_SYMBOL_PATH to be set (game PDB dir + MS symbol server cache) for function names.
//
// Implementation notes (verified against TraceEvent 3.0.5, perfview repo tag v3.0.5):
//  1. MutableTraceEventStackSource(TraceLog) creates an EMPTY stack source; per its API contract
//     samples must be pushed in with AddSample followed by DoneAddingSamples before ForEach can
//     yield anything.  We therefore enumerate the kernel SampledProfile events ourselves and add
//     one sample per event (weight 1).  Events without a captured stack fall back to a one-frame
//     stack built from the instruction pointer.
//  2. Symbol lookup of native (C++) PDBs needs TraceEvent's native helper DLLs present beside this
//     exe: amd64\msdia140.dll + amd64\msvcp140.dll + amd64\vcruntime140*.dll (from the
//     Microsoft.Diagnostics.Tracing.TraceEvent nupkg build\native\amd64).  Without them PDB parsing
//     silently fails and every frame stays "module!?".
//  3. This exe targets .NET 4.0, whose default ServicePointManager.SecurityProtocol is TLS 1.0;
//     the MS symbol server (and its blob-storage redirect) requires TLS 1.2, so downloads fail
//     unless TLS 1.2 is enabled explicitly (done in Main).
using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Text;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Tracing.Parsers.Kernel;
using Microsoft.Diagnostics.Tracing.Stacks;
using Microsoft.Diagnostics.Symbols;

class Program
{
    static void Add(Dictionary<string, double> d, string key, double count)
    {
        double v;
        if (d.TryGetValue(key, out v)) d[key] = v + count; else d[key] = count;
    }

    static string Csv(string s)
    {
        if (s == null) return "";
        if (s.IndexOf(',') >= 0 || s.IndexOf('"') >= 0)
            return "\"" + s.Replace("\"", "\"\"") + "\"";
        return s;
    }

    static List<KeyValuePair<string, double>> Sort(Dictionary<string, double> d)
    {
        var list = new List<KeyValuePair<string, double>>(d);
        list.Sort(delegate(KeyValuePair<string, double> a, KeyValuePair<string, double> b)
        { return b.Value.CompareTo(a.Value); });
        return list;
    }

    static void WriteCsv(string path, Dictionary<string, double> d, double total, int topN)
    {
        var list = Sort(d);
        using (var w = new StreamWriter(path, false, Encoding.UTF8))
        {
            w.WriteLine("function,samples,pct");
            int n = 0;
            foreach (var kv in list)
            {
                if (topN > 0 && n++ >= topN) break;
                w.WriteLine(string.Format("{0},{1},{2:F3}", Csv(kv.Key), kv.Value, 100.0 * kv.Value / total));
            }
        }
    }

    static int Main(string[] args)
    {
        if (args.Length < 3) { Console.WriteLine("usage: etl-hotspots <etl|etlx> <processFilter> <outPrefix> [topN]"); return 2; }
        string etl = args[0], filter = args[1], outPrefix = args[2];
        int topN = 0;
        if (args.Length >= 4) int.TryParse(args[3], out topN);

        // .NET 4.0 defaults to TLS 1.0, which the MS symbol server rejects; enable TLS 1.1/1.2.
        ServicePointManager.SecurityProtocol =
            (SecurityProtocolType)(0x000000C0 | 0x00000300 | 0x00000C00); // Tls | Tls11 | Tls12

        Console.WriteLine("opening " + etl);
        TraceLog log = TraceLog.OpenOrConvert(etl, null);
        Console.WriteLine("TraceLog created; enumerating CPU sampled-profile events");
        var src = new MutableTraceEventStackSource(log);

        int sampledEvents = 0, noStack = 0, added = 0;
        var sample = new StackSourceSample(src);
        sample.Metric = 1;
        foreach (var evt in log.Events)
        {
            var sp = evt as SampledProfileTraceData;
            if (sp == null) continue;
            sampledEvents++;
            if ((sampledEvents % 1000000) == 0)
                Console.WriteLine(string.Format("  ... {0:N0} sample events so far", sampledEvents));

            StackSourceCallStackIndex stackIdx = StackSourceCallStackIndex.Invalid;
            CallStackIndex csIdx = sp.CallStackIndex();
            if (csIdx != CallStackIndex.Invalid)
                stackIdx = src.GetCallStack(csIdx, sp);
            if (stackIdx == StackSourceCallStackIndex.Invalid)
            {
                // No stack captured for this sample: use a one-frame stack from the IP.
                CodeAddressIndex caIdx = sp.IntructionPointerCodeAddressIndex();
                if (caIdx != CodeAddressIndex.Invalid)
                {
                    StackSourceFrameIndex frameIdx = src.GetFrameIndex(caIdx);
                    stackIdx = src.Interner.CallStackIntern(frameIdx, StackSourceCallStackIndex.Invalid);
                }
            }
            if (stackIdx == StackSourceCallStackIndex.Invalid) { noStack++; continue; }
            sample.StackIndex = stackIdx;
            sample.TimeRelativeMSec = sp.TimeStampRelativeMSec;
            src.AddSample(sample);
            added++;
        }
        src.DoneAddingSamples();
        Console.WriteLine(string.Format("sample events: {0:N0}; added to stack source: {1:N0}; without resolvable address: {2:N0}",
            sampledEvents, added, noStack));

        Console.WriteLine("resolving symbols (may download from MS symbol server on first run)");
        var symPath = Environment.GetEnvironmentVariable("_NT_SYMBOL_PATH");
        var reader = new SymbolReader(TextWriter.Null, symPath, null);
        try { src.LookupWarmSymbols(1, reader, src, null); }
        catch (Exception e) { Console.WriteLine("NOTE: LookupWarmSymbols: " + e.Message); }
        Console.WriteLine("symbol resolution done; aggregating");

        var self = new Dictionary<string, double>();
        var incl = new Dictionary<string, double>();
        double total = 0, matched = 0;
        src.ForEach(delegate(StackSourceSample s)
        {
            total += s.Metric;
            var idx = s.StackIndex;
            var frames = new List<string>();
            bool isGame = false;
            int guard = 0;
            while (idx != StackSourceCallStackIndex.Invalid && guard < 1024)
            {
                guard++;
                var fi = src.GetFrameIndex(idx);
                string name = src.GetFrameName(fi, false);
                frames.Add(name);
                if (!isGame && name != null && name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0)
                    isGame = true;
                idx = src.GetCallerIndex(idx);
            }
            if (!isGame || frames.Count == 0) return;
            matched += s.Metric;
            Add(self, frames[0], s.Metric);
            var seen = new HashSet<string>();
            foreach (var f in frames) if (seen.Add(f)) Add(incl, f, s.Metric);
        });

        Console.WriteLine(string.Format("total samples: {0:F0}; matching '{1}': {2:F0} ({3:F2}%)",
            total, filter, matched, total > 0 ? 100.0 * matched / total : 0));
        if (matched > 0)
        {
            WriteCsv(outPrefix + "-self.csv", self, matched, topN);
            WriteCsv(outPrefix + "-incl.csv", incl, matched, topN);
            Console.WriteLine("wrote " + outPrefix + "-self.csv and " + outPrefix + "-incl.csv");
            Console.WriteLine("top self:");
            int n = 0;
            foreach (var kv in Sort(self))
            {
                if (n++ >= 25) break;
                Console.WriteLine(string.Format("  {0,7:F2}%  {1}", 100.0 * kv.Value / matched, kv.Key));
            }
        }
        return matched > 0 ? 0 : 1;
    }
}
