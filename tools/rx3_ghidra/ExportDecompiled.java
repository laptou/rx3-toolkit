// SPDX-License-Identifier: MPL-2.0
// write decompiled c for rbp functions, one file per function, with callers
// and callees in a header so the call graph can be followed file by file.
// args: output directory, then targets. a target is 0xADDRESS (the function
// containing it, created if ghidra has none), a function name, or str:TEXT
// (every function that references a defined string containing TEXT).
//@category RX3

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class ExportDecompiled extends GhidraScript {
    private static final int TIMEOUT_SECONDS = 120;
    private static final int STRING_FUNCTION_LIMIT = 40;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("usage: ExportDecompiled <output dir> <0xaddr|name|str:text>...");
            return;
        }
        Path output = Paths.get(args[0]);
        Files.createDirectories(output);
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try {
            for (int index = 1; index < args.length; index++) {
                for (Function function : resolve(args[index])) {
                    export(decompiler, output, function);
                }
            }
        } finally {
            decompiler.dispose();
        }
    }

    private Set<Function> resolve(String target) throws Exception {
        Set<Function> functions = new LinkedHashSet<>();
        if (target.startsWith("str:")) {
            String text = target.substring(4);
            DataIterator data = currentProgram.getListing().getDefinedData(true);
            while (data.hasNext() && functions.size() < STRING_FUNCTION_LIMIT) {
                Data item = data.next();
                if (!item.hasStringValue()) {
                    continue;
                }
                Object value = item.getValue();
                if (value == null || !value.toString().contains(text)) {
                    continue;
                }
                for (Reference reference : getReferencesTo(item.getAddress())) {
                    Function user = getFunctionContaining(reference.getFromAddress());
                    if (user != null) {
                        functions.add(user);
                    }
                }
            }
            if (functions.isEmpty()) {
                printerr("no function references a string containing: " + text);
            }
            return functions;
        }
        if (target.startsWith("0x")) {
            Address address = toAddr(Long.parseUnsignedLong(target.substring(2), 16));
            Function function = getFunctionContaining(address);
            if (function == null) {
                disassemble(address);
                function = createFunction(address, null);
            }
            if (function == null) {
                printerr("no function at " + target);
            } else {
                functions.add(function);
            }
            return functions;
        }
        List<Function> named = getGlobalFunctions(target);
        if (named.isEmpty()) {
            printerr("no function named " + target);
        }
        functions.addAll(named);
        return functions;
    }

    private void export(DecompInterface decompiler, Path output, Function function)
            throws Exception {
        StringBuilder text = new StringBuilder();
        text.append("// ").append(function.getName()).append(" @ ")
            .append(function.getEntryPoint()).append('\n');
        text.append("// callers:");
        for (Function caller : function.getCallingFunctions(monitor)) {
            text.append(' ').append(caller.getName()).append('@').append(caller.getEntryPoint());
        }
        text.append("\n// callees:");
        for (Function callee : function.getCalledFunctions(monitor)) {
            text.append(' ').append(callee.getName()).append('@').append(callee.getEntryPoint());
        }
        text.append("\n\n");
        DecompileResults result = decompiler.decompileFunction(function, TIMEOUT_SECONDS, monitor);
        if (result.decompileCompleted()) {
            text.append(result.getDecompiledFunction().getC());
        } else {
            text.append("// decompile failed: ").append(result.getErrorMessage()).append('\n');
        }
        String name = String.format("%08x_%s.c", function.getEntryPoint().getOffset(),
            function.getName().replaceAll("[^A-Za-z0-9_.-]", "_"));
        Files.writeString(output.resolve(name), text.toString());
        println("wrote " + output.resolve(name));
    }
}
