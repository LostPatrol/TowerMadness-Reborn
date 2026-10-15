// Export bounded original functions for the correction audit; never save the database.
// @category TowerMadness
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.io.PrintWriter;
import com.google.gson.*;
import java.util.*;

public class CorrectionProbe extends GhidraScript {
    // Only ownership and serialization boundaries under investigation.
    private static final Set<String> NAMES = new LinkedHashSet<>(Arrays.asList(
        "IBS_readBlock8Alloc", "IBS_readBlock32Alloc", "IBS_readBlock16Alloc",
        "AnimalsSyncData_fromIBS", "ANIMALS_alloc", "ANIMALS_free", "ENEMIES_alloc",
        "ENEMY_fromIBS", "GAME_update", "GAME_updatePlaySpeed", "GAME_resume", "TOWER_alloc", "TOWERS_toOBS", "TOWERS_fromIBS"));

    /** Export assembly and decompiler output using ELF symbol lengths, not inferred neighbors. */
    public void run() throws Exception {
        Path root = Path.of(getScriptArgs()[0]);
        JsonObject json = JsonParser.parseString(Files.readString(Path.of(getScriptArgs()[1]))).getAsJsonObject();
        Map<String, long[]> symbols = new LinkedHashMap<>();
        for (String section : new String[]{"symbols", "gameFunctions"}) {
            if (!json.has(section)) continue;
            for (JsonElement item : json.getAsJsonArray(section)) {
                JsonObject s = item.getAsJsonObject();
                String name = s.get("name").getAsString();
                if (NAMES.contains(name) && s.has("type") && s.get("type").getAsInt() == 2
                    && s.get("size").getAsLong() > 0 && s.get("sectionIndex").getAsInt() != 0)
                    symbols.put(name, new long[]{s.get("address").getAsLong(), s.get("size").getAsLong()});
            }
        }
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(root.resolve("ownership-export.txt")))) {
            for (Map.Entry<String,long[]> entry : symbols.entrySet()) {
                Address start = toAddr(entry.getValue()[0] + 0x10000);
                Address end = start.add(entry.getValue()[1] - 1);
                AddressSet body = new AddressSet(start, end);
                new DisassembleCommand(start, body, true).applyTo(currentProgram, monitor);
                Function f = getFunctionAt(start);
                if (f == null) f = createFunction(start, entry.getKey());
                f.setBody(body);
                f.setNoReturn(false);
                out.printf("\n=== %s ELF=%x size=%d ===\n",entry.getKey(),entry.getValue()[0],entry.getValue()[1]);
                var result = d.decompileFunction(f, 30, monitor);
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                else out.println(result.getErrorMessage());
                InstructionIterator instructions = currentProgram.getListing().getInstructions(body, true);
                while (instructions.hasNext()) {
                    Instruction i = instructions.next();
                    out.println(i.getAddress() + " " + i);
                }
                out.flush();
            }
        } finally { d.dispose(); }
    }
}
