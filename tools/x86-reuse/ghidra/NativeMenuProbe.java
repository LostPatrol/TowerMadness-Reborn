// Export the original MENU/MAPMENU/MENUWORLD object code and MENU_alloc callers.
// Function bodies are bounded by the real ELF symbol sizes from libverde-elf.json
// (fields: table/name/address/size/type/binding/sectionIndex), never by blindly
// extending to the next Ghidra function. The original database is only read;
// metadata corrections are discarded because headless runs without -save.
// @category TowerMadness
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

public class NativeMenuProbe extends GhidraScript {
    // Menu, level-select, and menu-world exports that the outer lifecycle needs.
    private static final String[] NAMES = {
        "MENU_alloc", "MENU_free", "MENU_init", "MENU_reset", "MENU_setResolution",
        "MENU_show", "MENU_hide", "MENU_startGame", "MENU_backToMainMenu", "MENU_changePage",
        "MENU_render", "MENU_update", "MENU_resume", "MENU_finishEditing",
        "MENU_handleTouchBegan", "MENU_handleTouchEnded", "MENU_handleTouchMoved",
        "MENU_endGame", "MENU_restartMap", "MENU_switchToPaidMainMenu",
        "MENU_drawGenericButton", "MENU_drawGenericButtonStringCentered",
        "MENU_showCredits", "MENU_showScores", "MENU_showTowerpedia", "MENU_showNewGameAlert",
        "MAPMENU_init", "MAPMENU_free", "MAPMENU_update", "MAPMENU_render", "MAPMENU_renderMap",
        "MAPMENU_restartMap", "MAPMENU_backButton", "MAPMENU_buildDLCList",
        "MAPMENU_touchesBegan", "MAPMENU_touchesMoved", "MAPMENU_touchesEnded",
        "MENUWORLD_alloc", "MENUWORLD_free", "MENUWORLD_setResolution", "MENUWORLD_update",
        "MENUWORLD_render", "MENUWORLD_timestep", "MENUWORLD_resume",
        "GAME_save", "GAME_fromIBS", "GAME_upgradeTower", "GAME_sellTower",
        // The campaign build path: confirm the argument order/cell space before driving it from the host.
        "GAME_buildTower", "GAME_touchesEnded", "TOWERS_build", "TOWERS_add",
        // Queued command processors: they decide the real argument semantics (cell units, ids, costs).
        "GAME_processBuildCommand", "GAME_processUpgradeCommand", "GAME_processSellCommand", "GAME_queueCommand",
        // TOWERS side of the queued commands: settle the upgrade argument order and the towers object layout.
        "TOWERS_tryBuild", "TOWERS_tryUpgrade", "TOWERS_getTowerForId", "TOWERS_getTowerForCell",
        "GAME_togglePlaySpeed", "GAME_hasEnded", "GAME_isWon",
        // Speed consumers: who actually READS GAME+0xa68 (the toggle only writes it). Needed to decide between
        // reusing the original playback contract and documenting the toggle as an app-side control. Read-only.
        "GAME_update", "GAME_updatePlaySpeed", "GAME_timestep",
        "GAME_getFinalScore", "GAME_getFinalLifes", "GAME_getGameTime",
        "unlockMedium", "unlockHard", "unlockInsane", "unlockEndOfGame",
        "GAMEMODE_load", "GAMEMODE_createEndless",
        "OBS_alloc", "OBS_writeUInt32", "OBS_writeBlock8", "OBS_grow",
        "IBS_wrap", "IBS_readUInt32",
        "GAME_alloc", "GAME_resume", "GAME_setResolution",
        "GAME_touchesBegan", "GAME_touchesMoved", "GAME_touchesEnded",
        "TUTORIAL_isEnabled", "TUTORIAL_isOver", "TUTORIAL_handleTap",
        "TUTORIAL_update", "TUTORIAL_render", "TUTORIAL_disable", "TUTORIAL_enable",
        "CAMERA_setWindowSize", "CAMERA_setAspect", "CAMERA_screenToWorld", "CAMERA_worldToScreen",
        // Save/continue subsystem bodies: the audit needs these to explain the cross-process restore.
        "GAMEMODE_fromIBS", "GAMEMODE_hash", "GAMEMODE_toOBS", "MAP_load", "GAME_free",
        "ANIMALS_alloc", "ANIMAL_init", "ANIMALS_generateFieldPoint", "ANIMALS_fromIBS",
        "IBS_free", "IBS_readBlock8", "IBS_readBlock16", "IBS_readUInt8", "IBS_readFloat",
        "PLAYER_fromIBS", "TOWERS_fromIBS", "ENEMIES_fromIBS", "PROJECTILES_fromIBS",
        "WAVEPREVIEW_fromIBS", "CAMERA_fromIBS", "REPLAY_fromIBS", "TUTORIAL_fromIBS",
        "MENUWORLD_alloc", "PGL_init", "TOWERS_getIdForCell",
        // Endgame score path (MENU_endGame calls these right before the unexplained alloc).
        "SCORES_alloc", "SCORES_free", "SCORES_load", "SCORES_save", "SCORES_wouldAdd",
        "SCORES_add", "CheckAndUpdateAchievements",
        // Crash sites under investigation: TOWERS_alloc+0x7C faults near simulation 67s, and the endgame
        // score branch calls REPLAY_getData right before the libSystem 16-byte bcopy fault.
        "TOWERS_alloc", "TOWERS_free", "TOWERS_build", "TOWERS_addTower", "TOWERS_update",
        "REPLAY_getData", "REPLAY_finish", "REPLAY_flushTimesteps", "REPLAY_alloc", "REPLAY_init",
        "REPLAY_toOBS", "REPLAY_startNextTimestep", "REPLAY_finishTimestep", "REPLAY_grow"
    };
    // These call sites reveal the original menu construction order and arguments.
    private static final String[] CALLER_TARGETS = {
        "MENU_alloc", "MENU_init", "MENU_show", "MENU_startGame", "MENU_endGame", "MAPMENU_init", "GAME_fromIBS"
    };
    // Raw function-pointer/data tables whose readers must be identified (save/continue dispatch).
    private static final long[] DATA_TARGETS = { 0x3c3484 };
    // Unnamed functions discovered from runtime logs (Ghidra addresses).
    private static final long[] ADDRESS_TARGETS = { 0x2ae870, 0x2c52c6, 0x2c6403 };
    // MENU_endGame tail: the runtime fault at ELF 0x2b4857 (Ghidra 0x2c4857) is inside the
    // non-game-center score branch, which the first pass left undisassembled.
    private static final long MENU_TAIL_START = 0x2c4801L, MENU_TAIL_END = 0x2c4950L;
    // Bounded raw range around the GAME_fromIBS reference, plus the exact probe address.
    private static final long RAW_RANGE_START = 0x3c3400L, RAW_RANGE_END = 0x3c3600L, RAW_PROBE_ADDRESS = 0x3c3484L;
    // Real dispatch callers are found statically because the only Ghidra reference is the .eh_frame FDE.
    private static final String[] DISPATCH_SCAN_NAMES = { "GAME_fromIBS", "GAME_save", "GAMEMODE_fromIBS", "GAME_alloc" };
    // Undisassembled functions that the audit flags (the MAPMENU start callback region).
    private static final long[] EXTRA_FUNCTION_ADDRESSES = { 0x2c52c6L, 0x2b6400L, 0x2c6400L };
    // Selector slots the decompiler showed as anonymous; export the referenced selector strings.
    private static final long[] SELECTOR_SLOTS = {
        0x3ec1e0, 0x3ec1d8, 0x3ec248, 0x3ec24c, 0x3ec25c,
        0x3ec19c, 0x3ec1a0, 0x3ec2c0, 0x3ec2b8, 0x3ec288, 0x3ec29c, 0x3ec2ac,
        0x3ec260, 0x3ec2c8, 0x3ec2c4, 0x3eb964, 0x3eb9b8, 0x3ebe2c, 0x3ec188
    };
    // Static page tables; pointers at +0/+4/+8/+0xc/+0x10/+0x14 are the page callbacks.
    private static final String[] PAGE_SYMBOLS = {
        "mainMenuPage", "mainMenuPageGameInProgress", "startGamePage", "endOfGamePage", "blackPage", "optionsMenuPage"
    };
    // Local (non-.dynsym) helpers called by the page render callbacks.
    private static final String[] LOCAL_NAMES = { "drawTabs", "drawLogo", "loadGamemode", "unlockedMedium", "unlockedHard", "unlockedInsane", "unlockedEndOfGame" };
    // Float layout constants used by the menu page callbacks (Ghidra addresses).
    private static final long[] MENU_CONSTANTS = {
        0x372a10, 0x372a18, 0x372b70, 0x372b7c, 0x372be4, 0x372bf8,
        0x372e34, 0x372e38, 0x372e3c, 0x372e40, 0x372e44, 0x372e48, 0x372e4c,
        0x372e60, 0x372e74, 0x372e78, 0x372e80, 0x372e84, 0x372e8c, 0x372e94,
        0x372e98, 0x372e9c, 0x372ea0, 0x372ea4, 0x372ea8, 0x372eac
    };
    private static final String DEFAULT_SYMBOLS = "E:/TowerMadness-Reborn/agent/codex/x86-reuse/libverde-elf.json";
    // Ghidra's x86 image base is shifted from the raw ELF virtual addresses.
    private static final long DEFAULT_IMAGE_OFFSET = 0x10000L;

    /** Keep the entry with the largest size for duplicate symbols. */
    private static void addSymbol(Map<String, long[]> symbols, String name, long address, long size) {
        long[] prior = symbols.get(name);
        if (prior == null || prior[1] < size) symbols.put(name, new long[] { address, size });
    }

    /** Parse the JSON with Ghidra's bundled Gson for one ELF symbol type (2=FUNC, 1=OBJECT). */
    private static Map<String, long[]> parseWithGson(String text, int wantedType) {
        Map<String, long[]> symbols = new LinkedHashMap<>();
        JsonObject root = JsonParser.parseString(text).getAsJsonObject();
        for (String arrayName : new String[] { "symbols", "gameFunctions" }) {
            JsonElement section = root.get(arrayName);
            if (section == null || !section.isJsonArray()) continue;
            for (JsonElement item : section.getAsJsonArray()) {
                if (!item.isJsonObject()) continue;
                JsonObject entry = item.getAsJsonObject();
                if (!entry.has("name") || !entry.has("address") || !entry.has("size")) continue;
                int type = entry.has("type") ? entry.get("type").getAsInt() : 0;
                int sectionIndex = entry.has("sectionIndex") ? entry.get("sectionIndex").getAsInt() : 0;
                long size = entry.get("size").getAsLong();
                if (type != wantedType || size <= 0 || sectionIndex == 0) continue;
                addSymbol(symbols, entry.get("name").getAsString(), entry.get("address").getAsLong(), size);
            }
        }
        return symbols;
    }

    /** Read the quoted value after the field colon, not the key before it. */
    private static String stringValue(String line) {
        int colon = line.indexOf(':');
        if (colon < 0) return null;
        String value = line.substring(colon + 1).trim();
        if (!value.startsWith("\"")) return null;
        int end = value.indexOf('"', 1);
        return end < 0 ? null : value.substring(1, end);
    }

    /** Read the numeric value after the field colon, stripping JSON punctuation. */
    private static long numberValue(String line) {
        int colon = line.lastIndexOf(':');
        String value = line.substring(colon + 1).replace(",", "").replace("}", "").trim();
        return Long.parseLong(value);
    }

    /** Fallback parser used only if Gson is unavailable in this Ghidra build. */
    private static Map<String, long[]> parseLineByLine(String text, int wantedType) {
        Map<String, long[]> symbols = new LinkedHashMap<>();
        String name = null;
        long address = -1, size = -1;
        int type = -1, section = -1;
        for (String raw : text.split("\\R")) {
            String line = raw.trim();
            if (line.startsWith("{")) { name = null; address = -1; size = -1; type = -1; section = -1; }
            else if (line.startsWith("\"name\"")) name = stringValue(line);
            else if (line.startsWith("\"address\"")) address = numberValue(line);
            else if (line.startsWith("\"size\"")) size = numberValue(line);
            else if (line.startsWith("\"type\"")) type = (int) numberValue(line);
            else if (line.startsWith("\"sectionIndex\"")) section = (int) numberValue(line);
            else if (line.startsWith("}") && name != null && type == wantedType && size > 0 && section != 0) {
                addSymbol(symbols, name, address, size);
            }
        }
        return symbols;
    }

    /** Parse .dynsym entries of one type from libverde-elf.json keyed by symbol name. */
    private static Map<String, long[]> parseElfSymbols(Path json, int wantedType) throws Exception {
        String text = Files.readString(json, StandardCharsets.UTF_8);
        try {
            return parseWithGson(text, wantedType);
        } catch (Throwable error) {
            System.out.println("MENU_PROBE GSON_FALLBACK " + error);
            return parseLineByLine(text, wantedType);
        }
    }

    /** Correct the i386 Objective-C dispatcher noreturn flags; tolerate read-only programs. */
    private boolean correctObjcNoReturn(PrintWriter status) {
        try {
            for (FunctionIterator iterator : new FunctionIterator[] {
                    currentProgram.getFunctionManager().getFunctions(true),
                    currentProgram.getFunctionManager().getExternalFunctions()}) {
                while (iterator.hasNext()) {
                    Function function = iterator.next();
                    if (function.getName().startsWith("objc_msgSend")) function.setNoReturn(false);
                }
            }
            return true;
        } catch (Exception error) {
            status.printf("objc_noreturn\tSKIPPED\t%s%n", error.getMessage());
            return false;
        }
    }

    /** Disassemble, bound, and decompile one original export using its real ELF symbol size. */
    private void export(DecompInterface decompiler, PrintWriter code, PrintWriter assembly, PrintWriter symbols,
            PrintWriter status, String name, Map<String, long[]> elf, long imageOffset) throws Exception {
        long[] entry = elf.get(name);
        if (entry == null) {
            symbols.printf("%s\tMISSING_IN_ELF%n", name);
            status.printf("%s\tMISSING_IN_ELF\t0\t0\t0\t\t\t\t\t\t%n", name);
            println("MENU_PROBE MISSING_IN_ELF " + name);
            return;
        }
        long elfAddress = entry[0], elfSize = entry[1];
        Address start = toAddr(elfAddress + imageOffset);
        symbols.printf("%s\t%x\t%d\t%s%n", name, elfAddress, elfSize, start);
        MemoryBlock block = currentProgram.getMemory().getBlock(start);
        String blockName = block == null ? "(none)" : block.getName();
        if (block == null || !block.isExecute() || blockName.startsWith(".plt")) {
            status.printf("%s\t%x\t%d\t%s\t%s\t%s\tfalse\tfalse\tfalse\tfalse\tRejectedNonImplementation%n",
                name, elfAddress, elfSize, start, blockName, block != null && block.isExecute());
            println("MENU_PROBE REJECTED " + name + " block=" + blockName);
            return;
        }
        Address end = start.add(elfSize - 1);
        AddressSet body = new AddressSet(start, end);
        Function function = currentProgram.getFunctionManager().getFunctionAt(start);
        boolean created = false, bodySet = false, disassembled = false;
        String note = "";
        try {
            if (function == null) { function = createFunction(start, name); created = function != null; }
        } catch (Exception error) {
            note = "create:" + error.getClass().getSimpleName();
        }
        if (function == null) {
            status.printf("%s\t%x\t%d\t%s\t%s\tfalse\tfalse\tfalse\tfalse\tfalse\t%s%n",
                name, elfAddress, elfSize, start, blockName, note);
            println("MENU_PROBE NOCREATE " + name + " " + note);
            return;
        }
        try {
            InstructionIterator oldInstructions = currentProgram.getListing().getInstructions(body, true);
            while (oldInstructions.hasNext()) {
                Instruction instruction = oldInstructions.next();
                if (instruction.getMnemonicString().equalsIgnoreCase("CALL")) instruction.setFlowOverride(FlowOverride.NONE);
            }
            new DisassembleCommand(start, body, true).applyTo(currentProgram, monitor);
            disassembled = true;
        } catch (Exception error) {
            note += " disasm:" + error.getClass().getSimpleName();
        }
        try {
            function.setBody(body);
            bodySet = true;
        } catch (Exception error) {
            note += " body:" + error.getClass().getSimpleName();
        }
        long actualSize = function.getBody().getNumAddresses();
        decompiler.openProgram(currentProgram);
        var result = decompiler.decompileFunction(function, 180, monitor);
        boolean complete = result.decompileCompleted();
        status.printf("%s\t%x\t%d\t%s\t%s\ttrue\t%s\t%s\t%s\t%s\telfSize=%d actualSize=%d thunk=%s%s%n",
            name, elfAddress, elfSize, start, blockName, created, disassembled, bodySet, complete,
            elfSize, actualSize, function.isThunk(), note.isEmpty() ? "" : (" " + note));
        println("MENU_PROBE EXPORTED " + name + " @" + start + " size=" + elfSize + " complete=" + complete
            + " bodySet=" + bodySet + " thunk=" + function.isThunk());
        code.printf("/* %s @ %s; elfSize=%d; complete=%s; bodySet=%s */%n", name, start, elfSize, complete, bodySet);
        code.println(complete ? result.getDecompiledFunction().getC() : result.getErrorMessage());
        InstructionIterator instructions = currentProgram.getListing().getInstructions(body, true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            assembly.printf("%s\t%s\t%s%n", name, instruction.getAddress(), instruction);
        }
    }

    /** List references to a target and decompile each containing function for startup order. */
    private void exportCallers(DecompInterface decompiler, PrintWriter callers, PrintWriter callerCode, String name,
            Map<String, long[]> elf, long imageOffset) throws Exception {
        long[] entry = elf.get(name);
        if (entry == null) { callers.printf("%s\tMISSING_IN_ELF%n", name); return; }
        Address start = toAddr(entry[0] + imageOffset);
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(start);
        Set<Address> functions = new LinkedHashSet<>();
        while (references.hasNext()) {
            Reference reference = references.next();
            Address from = reference.getFromAddress();
            Function container = currentProgram.getFunctionManager().getFunctionContaining(from);
            callers.printf("%s\t%s\t%s\t%s%n", name, from, reference.getReferenceType(),
                container == null ? "(no function)" : container.getName());
            if (container != null) functions.add(container.getEntryPoint());
        }
        for (Address entryPoint : functions) {
            Function function = currentProgram.getFunctionManager().getFunctionAt(entryPoint);
            if (function == null) continue;
            decompiler.openProgram(currentProgram);
            var result = decompiler.decompileFunction(function, 180, monitor);
            callerCode.printf("/* caller %s @ %s; complete=%s */%n", function.getName(), entryPoint, result.decompileCompleted());
            callerCode.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
        }
    }

    /** Export the selector strings behind data slots the decompiler showed as anonymous. */
    private void exportSelectorSlots(PrintWriter out) {
        for (long slot : SELECTOR_SLOTS) {
            try {
                int pointer = currentProgram.getMemory().getInt(toAddr(slot));
                StringBuilder text = new StringBuilder();
                if (pointer != 0) {
                    byte[] buffer = new byte[128];
                    int count = currentProgram.getMemory().getBytes(toAddr(pointer), buffer);
                    for (int index = 0; index < count && buffer[index] != 0; ++index) text.append((char) (buffer[index] & 0xff));
                }
                out.printf("%x\t%x\t%s%n", slot, pointer, text);
            } catch (Exception error) {
                out.printf("%x\tERROR\t%s%n", slot, error.getMessage());
            }
        }
    }

    /** Bounded body expansion for local page callbacks that have no ELF symbol size. */
    private void expandFunction(Function function) {
        Address start = function.getEntryPoint();
        FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(start, true);
        Address limit = start.add(0x800);
        while (iterator.hasNext()) {
            Address entry = iterator.next().getEntryPoint();
            if (entry.compareTo(start) > 0) { limit = entry.subtract(1); break; }
        }
        AddressSet body = new AddressSet(start, limit);
        try {
            InstructionIterator old = currentProgram.getListing().getInstructions(body, true);
            while (old.hasNext()) {
                Instruction instruction = old.next();
                if (instruction.getMnemonicString().equalsIgnoreCase("CALL")) instruction.setFlowOverride(FlowOverride.NONE);
            }
            new DisassembleCommand(start, body, true).applyTo(currentProgram, monitor);
            function.setBody(body);
        } catch (Exception error) {
            println("MENU_PROBE PAGE_BODY_SKIPPED " + function.getName() + " " + error);
        }
    }

    /** Export the page callback pointers and decompile each referenced function. */
    private void exportPageCallbacks(DecompInterface decompiler, PrintWriter slots, PrintWriter code, Map<String, long[]> elf) {
        for (String name : PAGE_SYMBOLS) {
            long[] entry = elf.get(name);
            if (entry == null) { slots.printf("%s\tMISSING_IN_ELF%n", name); continue; }
            Address table = toAddr(entry[0] + DEFAULT_IMAGE_OFFSET);
            for (int offset = 0; offset <= 0x14; offset += 4) {
                try {
                    int pointer = currentProgram.getMemory().getInt(table.add(offset));
                    slots.printf("%s\t+0x%x\t%x\t%s%n", name, offset, pointer, pointer == 0 ? "0" : toAddr(pointer).toString());
                    if (pointer == 0) continue;
                    Address entryPoint = toAddr(pointer);
                    Function function = currentProgram.getFunctionManager().getFunctionContaining(entryPoint);
                    if (function == null) function = currentProgram.getFunctionManager().getFunctionAt(entryPoint);
                    if (function == null) { code.printf("/* %s+0x%x -> %s (no function) */%n", name, offset, entryPoint); continue; }
                    expandFunction(function);
                    decompiler.openProgram(currentProgram);
                    var result = decompiler.decompileFunction(function, 180, monitor);
                    code.printf("/* %s+0x%x -> %s @ %s; complete=%s */%n", name, offset, function.getName(),
                        function.getEntryPoint(), result.decompileCompleted());
                    code.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
                } catch (Exception error) {
                    slots.printf("%s\t+0x%x\tERROR\t%s%n", name, offset, error.getMessage());
                }
            }
        }
    }

    /** Export the float layout constants used by the menu page callbacks. */
    private void exportConstants(PrintWriter out) {
        for (long address : MENU_CONSTANTS) {
            try {
                int bits = currentProgram.getMemory().getInt(toAddr(address));
                out.printf("%x\t%08x\t%+.6g\t%d%n", address, bits, Float.intBitsToFloat(bits), bits);
            } catch (Exception error) {
                out.printf("%x\tERROR\t%s%n", address, error.getMessage());
            }
        }
    }

    /** Dump a raw pointer table and every code/data reference to it. */
    private void exportDataTargets(PrintWriter out) {
        for (long address : DATA_TARGETS) {
            Address target = toAddr(address);
            out.printf("DATA %x%n", address);
            for (long offset = -0x10; offset <= 0x30; offset += 4) {
                try {
                    int value = currentProgram.getMemory().getInt(target.add(offset));
                    out.printf("  +%x %08x%s%n", offset, value,
                        value >= 0x10000 ? (" -> " + toAddr(value & 0xffffffffL)) : "");
                } catch (Exception error) { out.printf("  +%x ERR%n", offset); }
            }
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
            while (references.hasNext()) {
                Reference reference = references.next();
                Function owner = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
                out.printf("  REF from %s in %s%n", reference.getFromAddress(), owner != null ? owner.getName() : "(no function)");
            }
        }
    }

    /** Disassemble the MENU_endGame tail so a runtime fault offset can be mapped to an instruction. */
    private void exportMenuTail(PrintWriter out) {
        Address start = toAddr(MENU_TAIL_START), end = toAddr(MENU_TAIL_END);
        out.println("MENU_TAIL " + start + ".." + end);
        try { new DisassembleCommand(start, new AddressSet(start, end), true).applyTo(currentProgram, monitor); }
        catch (Exception error) { out.println("DISASM_ERROR " + error); }
        InstructionIterator instructions = currentProgram.getListing().getInstructions(new AddressSet(start, end), true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            out.println(instruction.getAddress() + "\t" + instruction.toString());
        }
    }

    /** Decompile unnamed functions by raw address (runtime stack-trace callers). */
    private void exportAddressTargets(DecompInterface decompiler, PrintWriter code, PrintWriter status) throws Exception {
        for (long address : ADDRESS_TARGETS) {
            Address target = toAddr(address);
            Function function = currentProgram.getFunctionManager().getFunctionContaining(target);
            if (function == null) function = currentProgram.getFunctionManager().getFunctionAt(target);
            if (function == null) { status.printf("addr_%x\tNO_FUNCTION%n", address); continue; }
            expandFunction(function);
            decompiler.openProgram(currentProgram);
            var result = decompiler.decompileFunction(function, 180, monitor);
            status.printf("addr_%x\t%s\tcomplete=%s%n", address, function.getEntryPoint(), result.decompileCompleted());
            code.printf("/* address %x -> %s @ %s; complete=%s */%n", address, function.getName(), function.getEntryPoint(), result.decompileCompleted());
            code.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
        }
    }

    /** Locate a labeled local function symbol by exact name. */
    private Symbol findLocalSymbol(String name) {
        SymbolIterator symbols = currentProgram.getSymbolTable().getSymbols(name);
        while (symbols.hasNext()) {
            Symbol symbol = symbols.next();
            if (!symbol.isExternal()) return symbol;
        }
        return null;
    }

    /** Export a local helper by Ghidra symbol name, expanding its body first. */
    private void exportLocal(DecompInterface decompiler, PrintWriter code, PrintWriter status, String name) throws Exception {
        Symbol symbol = findLocalSymbol(name);
        if (symbol == null) { status.printf("%s\tMISSING_SYMBOL%n", name); return; }
        Function function = currentProgram.getFunctionManager().getFunctionAt(symbol.getAddress());
        if (function == null) function = currentProgram.getFunctionManager().getFunctionContaining(symbol.getAddress());
        if (function == null) { status.printf("%s\tNO_FUNCTION\t%s%n", name, symbol.getAddress()); return; }
        expandFunction(function);
        decompiler.openProgram(currentProgram);
        var result = decompiler.decompileFunction(function, 180, monitor);
        status.printf("%s\t%s\tcomplete=%s%n", name, function.getEntryPoint(), result.decompileCompleted());
        code.printf("/* local %s @ %s; complete=%s */%n", name, function.getEntryPoint(), result.decompileCompleted());
        code.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
    }

    /**
     * Walk .eh_frame and report every CIE/FDE. A raw address that sits in the pc_begin field of an
     * FDE is an exception-handling record, not a caller; GAME_fromIBS's only Ghidra reference is one.
     */
    private void exportEhFrame(PrintWriter out, long probeAddress) {
        MemoryBlock frame = null;
        for (MemoryBlock block : currentProgram.getMemory().getBlocks())
            if (block.getName().contains("eh_frame")) { frame = block; break; }
        if (frame == null) { out.println("NO_EH_FRAME"); return; }
        out.printf("EH_FRAME %s..%s probe=%x%n", frame.getStart(), frame.getEnd(), probeAddress);
        Address at = frame.getStart();
        final long limit = frame.getEnd().getOffset();
        int records = 0, matches = 0;
        while (at.getOffset() + 4 <= limit && records < 500000) {
            int length;
            try { length = currentProgram.getMemory().getInt(at); } catch (Exception error) { out.println("TRUNCATED " + at); break; }
            if (length == 0) { out.println("TERMINATOR " + at); break; }
            Address body = at.add(4);
            long next = body.getOffset() + (length & 0xffffffffL);
            if (next > limit) { out.println("OVERRUN " + at + " length=" + length); break; }
            try {
                int ciePointer = currentProgram.getMemory().getInt(body);
                if (ciePointer == 0) {
                    out.printf("CIE %s length=%d%n", at, length);
                } else {
                    Address pcField = body.add(4);
                    int pcBegin = currentProgram.getMemory().getInt(pcField);
                    int range = currentProgram.getMemory().getInt(body.add(8));
                    long pcValue = (pcField.getOffset() + pcBegin) & 0xffffffffL;
                    long cieValue = (body.getOffset() - (ciePointer & 0xffffffffL)) & 0xffffffffL;
                    boolean contains = probeAddress >= at.getOffset() && probeAddress < next;
                    out.printf("FDE %s length=%d cie=%s pcField=%s pc=%s range=0x%x%s%n", at, length,
                        toAddr(cieValue), pcField, toAddr(pcValue), range, contains ? " <== CONTAINS_PROBE" : "");
                    if (contains) {
                        ++matches;
                        out.printf("  PROBE %x kind=DW_EH_PE_pcrel|sdata4 pc_begin=%s range=0x%x%n",
                            probeAddress, toAddr(pcValue), range);
                    }
                }
            } catch (Exception error) { out.println("RECORD_ERROR " + at + " " + error); break; }
            ++records;
            at = toAddr(next);
        }
        out.println("RECORDS " + records + " PROBE_MATCHES " + matches);
    }

    /** Report the block/function around a raw address and dump a bounded range as hex and instructions. */
    private void exportRawRange(PrintWriter out) {
        Address probe = toAddr(RAW_PROBE_ADDRESS);
        MemoryBlock block = currentProgram.getMemory().getBlock(probe);
        out.printf("PROBE %x block=%s executable=%s initialized=%s start=%s end=%s%n", RAW_PROBE_ADDRESS,
            block == null ? "(none)" : block.getName(), block != null && block.isExecute(),
            block != null && block.isInitialized(), block == null ? "?" : block.getStart().toString(),
            block == null ? "?" : block.getEnd().toString());
        Function containing = currentProgram.getFunctionManager().getFunctionContaining(probe);
        Function before = null;
        FunctionIterator functionsAscending = currentProgram.getFunctionManager().getFunctions(true);
        while (functionsAscending.hasNext()) {
            Function candidate = functionsAscending.next();
            if (candidate.getEntryPoint().compareTo(probe) < 0) before = candidate; else break;
        }
        out.println("FUNCTION_CONTAINING " + (containing == null ? "(none)" : containing.getName() + "@" + containing.getEntryPoint()));
        out.println("FUNCTION_BEFORE " + (before == null ? "(none)" : before.getName() + "@" + before.getEntryPoint()));
        out.println("RAW_BYTES");
        for (long offset = RAW_RANGE_START; offset <= RAW_RANGE_END; offset += 16) {
            StringBuilder hex = new StringBuilder();
            StringBuilder ascii = new StringBuilder();
            for (int index = 0; index < 16 && offset + index <= RAW_RANGE_END; ++index) {
                try {
                    int value = currentProgram.getMemory().getByte(toAddr(offset + index)) & 0xff;
                    hex.append(String.format("%02x ", value));
                    ascii.append(value >= 32 && value < 127 ? (char) value : '.');
                } catch (Exception error) { hex.append("?? "); }
            }
            out.printf("%x  %-48s %s%n", offset, hex.toString(), ascii.toString());
        }
        // The region is .eh_frame data, so the disassembly is recorded as raw decode only; the ELF
        // image and the project are never saved, so these instructions cannot leak into a database.
        Address start = toAddr(RAW_RANGE_START), end = toAddr(RAW_RANGE_END);
        out.println("INSTRUCTIONS");
        try {
            new DisassembleCommand(start, new AddressSet(start, end), false).applyTo(currentProgram, monitor);
        } catch (Exception error) { out.println("DISASM_ERROR " + error); }
        InstructionIterator instructions = currentProgram.getListing().getInstructions(new AddressSet(start, end), true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            out.printf("%s\t%s%n", instruction.getAddress(), instruction);
        }
    }

    /** Scan executable memory for direct call/jump encodings that target the dispatch functions. */
    private void scanDirectCalls(PrintWriter out, Map<String, long[]> elf, long imageOffset) {
        Map<Long, String> targets = new java.util.TreeMap<>();
        for (String name : DISPATCH_SCAN_NAMES) {
            long[] entry = elf.get(name);
            if (entry != null) targets.put((entry[0] + imageOffset) & 0xffffffffL, name);
        }
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isExecute() || !block.isInitialized()) continue;
            Address at = block.getStart();
            final long limit = block.getEnd().getOffset();
            while (at.getOffset() + 5 <= limit) {
                int opcode;
                try { opcode = currentProgram.getMemory().getByte(at) & 0xff; } catch (Exception error) { break; }
                if (opcode == 0xe8 || opcode == 0xe9) {
                    int displacement;
                    try { displacement = currentProgram.getMemory().getInt(at.add(1)); } catch (Exception error) { break; }
                    long target = (at.getOffset() + 5 + displacement) & 0xffffffffL;
                    String name = targets.get(target);
                    if (name != null) {
                        Function owner = currentProgram.getFunctionManager().getFunctionContaining(at);
                        out.printf("%s\t%s\t%s\t%s%n", name, at, block.getName(),
                            owner == null ? "(no function)" : owner.getName() + "@" + owner.getEntryPoint());
                    }
                }
                at = at.add(1);
            }
        }
    }

    /** Scan all initialized memory for absolute pointers and DW_EH_PE_pcrel values to the functions. */
    private void scanPointers(PrintWriter out, Map<String, long[]> elf, long imageOffset) {
        Map<Long, String> targets = new java.util.TreeMap<>();
        for (String name : DISPATCH_SCAN_NAMES) {
            long[] entry = elf.get(name);
            if (entry != null) targets.put((entry[0] + imageOffset) & 0xffffffffL, name);
        }
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized()) continue;
            Address at = block.getStart();
            final long limit = block.getEnd().getOffset();
            while (at.getOffset() + 4 <= limit) {
                int value;
                try { value = currentProgram.getMemory().getInt(at); } catch (Exception error) { break; }
                String absolute = targets.get(value & 0xffffffffL);
                if (absolute != null) out.printf("ABS %s at %s stored=%08x block=%s%n", absolute, at, value, block.getName());
                long pcRelative = (at.getOffset() + value) & 0xffffffffL;
                String pcrel = targets.get(pcRelative);
                if (pcrel != null) out.printf("PCREL %s at %s stored=%08x block=%s%n", pcrel, at, value, block.getName());
                at = at.add(4);
            }
        }
    }

    /** Decompile the function containing a raw address (used for undisassembled dispatch regions). */
    private void exportFunctionAt(DecompInterface decompiler, PrintWriter out, long address) {
        Address target = toAddr(address);
        Function function = currentProgram.getFunctionManager().getFunctionContaining(target);
        if (function == null) function = currentProgram.getFunctionManager().getFunctionAt(target);
        if (function == null) {
            MemoryBlock block = currentProgram.getMemory().getBlock(target);
            out.printf("/* %x (no function) block=%s */%n", address, block == null ? "(none)" : block.getName());
            return;
        }
        expandFunction(function);
        decompiler.openProgram(currentProgram);
        var result = decompiler.decompileFunction(function, 180, monitor);
        out.printf("/* extra %x -> %s @ %s; complete=%s */%n", address, function.getName(),
            function.getEntryPoint(), result.decompileCompleted());
        out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
    }

    /** Resolve exact ELF ranges, then export menu code and caller startup order. */
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 1) { println("Usage: NativeMenuProbe.java OUTPUT_DIR [libverde-elf.json] [imageOffset]"); return; }
        Path output = Path.of(arguments[0]);
        Files.createDirectories(output);
        Path symbolJson = Path.of(arguments.length > 1 ? arguments[1] : DEFAULT_SYMBOLS);
        long imageOffset = arguments.length > 2 ? Long.decode(arguments[2]) : DEFAULT_IMAGE_OFFSET;
        Map<String, long[]> elf = parseElfSymbols(symbolJson, 2);
        // Page tables are OBJECT symbols (type 1), so they need their own map.
        Map<String, long[]> data = parseElfSymbols(symbolJson, 1);
        StringBuilder sample = new StringBuilder();
        int shown = 0;
        for (String name : elf.keySet()) { if (shown++ >= 5) break; sample.append(' ').append(name).append('=').append(elf.get(name)[1]); }
        println("MENU_PROBE SYMBOLS " + elf.size() + " from " + symbolJson + " offset=0x" + Long.toHexString(imageOffset)
            + " sample:" + sample);
        if (elf.size() < 100 || !elf.containsKey("MENU_alloc")) {
            println("MENU_PROBE PARSE_FAILED symbolCount=" + elf.size() + " hasMenuAlloc=" + elf.containsKey("MENU_alloc")
                + "; refusing to export with an unverified symbol table.");
            return;
        }
        DecompInterface decompiler = new DecompInterface();
        try (PrintWriter code = new PrintWriter(Files.newBufferedWriter(output.resolve("pseudocode.c"), StandardCharsets.UTF_8));
             PrintWriter assembly = new PrintWriter(Files.newBufferedWriter(output.resolve("instructions.tsv"), StandardCharsets.UTF_8));
             PrintWriter symbols = new PrintWriter(Files.newBufferedWriter(output.resolve("symbols.tsv"), StandardCharsets.UTF_8));
             PrintWriter callers = new PrintWriter(Files.newBufferedWriter(output.resolve("callers.tsv"), StandardCharsets.UTF_8));
             PrintWriter callerCode = new PrintWriter(Files.newBufferedWriter(output.resolve("caller-pseudocode.c"), StandardCharsets.UTF_8));
             PrintWriter selectorSlots = new PrintWriter(Files.newBufferedWriter(output.resolve("selector-slots.tsv"), StandardCharsets.UTF_8));
             PrintWriter pageSlots = new PrintWriter(Files.newBufferedWriter(output.resolve("page-slots.tsv"), StandardCharsets.UTF_8));
             PrintWriter pageCode = new PrintWriter(Files.newBufferedWriter(output.resolve("page-callbacks.c"), StandardCharsets.UTF_8));
             PrintWriter localCode = new PrintWriter(Files.newBufferedWriter(output.resolve("local-pseudocode.c"), StandardCharsets.UTF_8));
             PrintWriter constants = new PrintWriter(Files.newBufferedWriter(output.resolve("menu-constants.tsv"), StandardCharsets.UTF_8));
             PrintWriter dataRefs = new PrintWriter(Files.newBufferedWriter(output.resolve("data-refs.tsv"), StandardCharsets.UTF_8));
             PrintWriter addressCode = new PrintWriter(Files.newBufferedWriter(output.resolve("address-pseudocode.c"), StandardCharsets.UTF_8));
             PrintWriter menuTail = new PrintWriter(Files.newBufferedWriter(output.resolve("menu-tail.txt"), StandardCharsets.UTF_8));
             PrintWriter ehFrame = new PrintWriter(Files.newBufferedWriter(output.resolve("eh-frame.tsv"), StandardCharsets.UTF_8));
             PrintWriter directCalls = new PrintWriter(Files.newBufferedWriter(output.resolve("direct-call-scan.tsv"), StandardCharsets.UTF_8));
             PrintWriter pointerScan = new PrintWriter(Files.newBufferedWriter(output.resolve("pointer-scan.tsv"), StandardCharsets.UTF_8));
             PrintWriter extraCode = new PrintWriter(Files.newBufferedWriter(output.resolve("extra-pseudocode.c"), StandardCharsets.UTF_8));
             PrintWriter rawRange = new PrintWriter(Files.newBufferedWriter(output.resolve("raw-range.tsv"), StandardCharsets.UTF_8));
             PrintWriter status = new PrintWriter(Files.newBufferedWriter(output.resolve("script-status.tsv"), StandardCharsets.UTF_8))) {
            status.println("name\telfAddress\telfSize\tghidraAddress\tblock\texecute\tcreated\tdisassembled\tbodySet\tdecompileComplete\tnote");
            status.printf("objc_noreturn\tCORRECTED\t%s%n", correctObjcNoReturn(status));
            symbols.println("name\telfAddress\telfSize\tghidraAddress");
            callers.println("target\tfrom\treference\tcontaining_function");
            for (String name : NAMES) export(decompiler, code, assembly, symbols, status, name, elf, imageOffset);
            for (String name : CALLER_TARGETS) exportCallers(decompiler, callers, callerCode, name, elf, imageOffset);
            selectorSlots.println("slot\tpointer\tselector");
            exportSelectorSlots(selectorSlots);
            pageSlots.println("page\toffset\tpointer\tghidraTarget");
            exportPageCallbacks(decompiler, pageSlots, pageCode, data);
            for (String name : LOCAL_NAMES) exportLocal(decompiler, localCode, status, name);
            constants.println("ghidraAddress\trawBits\tasFloat\tasInt");
            exportConstants(constants);
            exportDataTargets(dataRefs);
            exportAddressTargets(decompiler, addressCode, status);
            exportMenuTail(menuTail);
            // Locate the real save/continue dispatch: the Ghidra reference is only the .eh_frame FDE,
            // so scan raw encodings for the actual call sites and pointer/PC-relative references.
            exportEhFrame(ehFrame, RAW_PROBE_ADDRESS);
            directCalls.println("target\tfrom\tblock\tcontaining_function");
            scanDirectCalls(directCalls, elf, imageOffset);
            pointerScan.println("# ABS value == function entry; PCREL value == target - location (DW_EH_PE_pcrel)");
            scanPointers(pointerScan, elf, imageOffset);
            for (long address : EXTRA_FUNCTION_ADDRESSES) exportFunctionAt(decompiler, extraCode, address);
            // Last: decoding .eh_frame as instructions mutates the in-memory listing (never saved).
            exportRawRange(rawRange);
        } finally {
            decompiler.dispose();
        }
        println("MENU_PROBE DONE " + output.toAbsolutePath());
    }
}
