// Run with analyzeHeadless ... -postScript ExportFunctionRanges.java <csv-path>
// Emits guest VA ranges and Ghidra names; no executable bytes leave the Ghidra project.
import java.io.File;
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportFunctionRanges extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("usage: ExportFunctionRanges.java <csv-path>");
        try (PrintWriter out = new PrintWriter(new File(args[0]), "UTF-8")) {
            out.println("# start,end,entry,name (guest VA; end exclusive)");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                Function function = functions.next();
                if (function.isExternal()) continue;
                Address entry = function.getEntryPoint();
                AddressRangeIterator ranges = function.getBody().getAddressRanges();
                while (ranges.hasNext()) {
                    AddressRange range = ranges.next();
                    Address end = range.getMaxAddress().next();
                    if (end == null || entry.getAddressSpace() != end.getAddressSpace()) continue;
                    out.printf("0x%s,0x%s,0x%s,%s%n", range.getMinAddress().toString(),
                               end.toString(), entry.toString(), function.getName());
                }
            }
        }
    }
}
