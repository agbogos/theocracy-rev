//Clears bogus "No Return" flags left by the Non-Returning Functions analyzer,
//and removes the fall-through overrides those flags stamped onto call sites.
//
//Why this is needed: Ghidra's "Non-Returning Functions - Discovered" analyzer
//misreads g++ 2.95 tail-call thunks (a tiny body ending in JMP) as functions
//that never return. In libmvos that wrongly flagged ordinary constructors,
//setters and list ops (tHNode, cDimension::Set, cVObject::Refresh,
//cList::UnLinkList, cNode::UnLink, __builtin_new, ...). Every caller then gets
//truncated at the first such call, so the decompiler shows only a fragment.
//
//Clearing the flag alone is NOT enough: the analyzer also records "no
//fall-through" on each call INSTRUCTION, and that override survives. This
//script undoes both.
//
//After running: Analysis -> Auto Analyze, or right-click -> Re-create Function
//on the functions you care about, so their bodies are re-walked.
//
//@category Repair

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class FixBogusNoReturn extends GhidraScript {

	// Functions that genuinely never return - leave these flagged.
	private static final Set<String> KEEP = new HashSet<>(Arrays.asList(
		"__throw", "terminate", "abort", "exit", "_exit", "__assert_fail",
		"longjmp", "siglongjmp", "__cxa_throw", "_Unwind_Resume"));

	@Override
	public void run() throws Exception {
		Listing listing = currentProgram.getListing();
		ReferenceManager refs = currentProgram.getReferenceManager();

		int cleared = 0;
		int sites = 0;
		StringBuilder log = new StringBuilder();

		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		while (it.hasNext()) {
			if (monitor.isCancelled()) {
				break;
			}
			Function f = it.next();
			if (!f.hasNoReturn() || KEEP.contains(f.getName())) {
				continue;
			}

			f.setNoReturn(false);
			cleared++;
			log.append("  ").append(f.getEntryPoint())
			   .append("  ").append(f.getName()).append("\n");

			// Undo the per-call-site fall-through suppression, otherwise callers
			// stay truncated even though the callee's flag is gone.
			Address entry = f.getEntryPoint();
			ReferenceIterator ri = refs.getReferencesTo(entry);
			while (ri.hasNext()) {
				if (monitor.isCancelled()) {
					break;
				}
				Reference r = ri.next();
				if (!r.getReferenceType().isCall()) {
					continue;
				}
				Instruction ins = listing.getInstructionAt(r.getFromAddress());
				if (ins != null && ins.isFallThroughOverridden()) {
					ins.clearFallThroughOverride();
					sites++;
				}
			}
		}

		// Pass 2 - sweep every call site in the program, not just the ones whose
		// callee this run un-flagged. A function un-flagged in an EARLIER pass
		// (or by hand in the UI) keeps its stale fall-through overrides, and
		// pass 1 never revisits it because hasNoReturn() is already false. Those
		// leftovers still truncate their callers, which is exactly how
		// External_PlayAnim stayed stuck at the cDimension::Set call.
		int stale = 0;
		InstructionIterator ii = listing.getInstructions(true);
		while (ii.hasNext()) {
			if (monitor.isCancelled()) {
				break;
			}
			Instruction ins = ii.next();
			boolean hasFlowOverride = ins.getFlowOverride() != FlowOverride.NONE;
			if (!hasFlowOverride && !ins.isFallThroughOverridden()) {
				continue;
			}
			for (Reference r : ins.getReferencesFrom()) {
				if (!r.getReferenceType().isCall()) {
					continue;
				}
				Function target = currentProgram.getFunctionManager()
					.getFunctionAt(r.getToAddress());
				if (target != null && !target.hasNoReturn()) {
					// CALL_RETURN is what the analyzer stamps on a call to a
					// noreturn function; it is what actually suppresses the
					// fall-through, and it survives clearing the callee's flag.
					if (hasFlowOverride) {
						ins.setFlowOverride(FlowOverride.NONE);
					}
					if (ins.isFallThroughOverridden()) {
						ins.clearFallThroughOverride();
					}
					stale++;
					break;
				}
			}
		}

		println("cleared noreturn on " + cleared + " functions, "
			+ sites + " call sites un-suppressed, "
			+ stale + " stale overrides swept");
		print(log.toString());
	}
}
