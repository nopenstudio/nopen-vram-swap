#!/usr/bin/env python3
"""v0.8.4: deterministic *hypothetical* reclaim wait-for graph.

This is NOT a kernel deadlock detector. Its edges are scenarios, not captured
kernel stacks. It never reads system memory, starts ublk or launches OpenCL.
"""
from __future__ import annotations
import argparse
import json

# Hypothesis: a kernel request to free a page needs the Nopen ublk server,
# which needs the worker, which needs OpenCL/driver to allocate host pages.
# Under pressure, that allocation may itself need the blocked reclaim.
POTENTIAL_RECLAIM_CYCLE = {
    "kernel_reclaim": ("ublk_completion",),
    "ublk_completion": ("nopen_ublk_server",),
    "nopen_ublk_server": ("gpu_worker",),
    "gpu_worker": ("opencl_driver",),
    "opencl_driver": ("host_allocation",),
    "host_allocation": ("kernel_reclaim",),
}
# A purely hypothetical preallocated independent service can break one edge
# but is NOT a real implementation of such a service.
HYPOTHETICAL_INDEPENDENT_RESERVE = {
    **POTENTIAL_RECLAIM_CYCLE,
    "opencl_driver": ("preallocated_independent_service",),
    "preallocated_independent_service": (),
}
HYPOTHETICAL_INDEPENDENT_RESERVE.pop("host_allocation")


def cycle_in_wait_graph(graph: dict[str, tuple[str, ...]]) -> list[str]:
    color: dict[str, int] = {}
    stack: list[str] = []
    position: dict[str, int] = {}

    def visit(vertex: str) -> list[str]:
        color[vertex] = 1
        position[vertex] = len(stack)
        stack.append(vertex)
        for next_vertex in graph.get(vertex, ()):
            if color.get(next_vertex, 0) == 1:
                return stack[position[next_vertex]:] + [next_vertex]
            if color.get(next_vertex, 0) == 0:
                found = visit(next_vertex)
                if found:
                    return found
        stack.pop()
        position.pop(vertex)
        color[vertex] = 2
        return []

    for vertex in sorted(set(graph).union(
            value for targets in graph.values() for value in targets)):
        if color.get(vertex, 0) == 0:
            found = visit(vertex)
            if found:
                return found
    return []


def evaluate(graph: dict[str, tuple[str, ...]], scenario: str) -> dict:
    cycle = cycle_in_wait_graph(graph)
    return {
        "schema": "NOPEN_V084_RECLAIM_CYCLE_HYPOTHESIS",
        "scenario": scenario,
        "evidence": "DETERMINISTIC_HYPOTHETICAL_WAIT_FOR_GRAPH",
        "edge_list": [[v, neighbor] for v, targets in sorted(graph.items())
                      for neighbor in targets],
        "cycle_present_in_model": bool(cycle),
        "cycle_path": cycle,
        "real_kernel_deadlock_observed": False,
        "opencl_runtime_reclaim_independence_proven": False,
        "actual_gpu_reset_survivability_proven": False,
        "kernel_swap_authorized": False,
        "safety_gate": "KERNEL_SWAP_BLOCKED",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=("cycle", "hypothetical-reserve",
                                               "both"), default="both")
    args = parser.parse_args()
    selected = []
    if args.scenario in ("both", "cycle"):
        selected.append(evaluate(POTENTIAL_RECLAIM_CYCLE,
                                 "UNVERIFIED_RECLAIM_CYCLE_HYPOTHESIS"))
    if args.scenario in ("both", "hypothetical-reserve"):
        selected.append(evaluate(HYPOTHETICAL_INDEPENDENT_RESERVE,
                                 "IDEALIZED_RECLAIM_INDEPENDENCE_NOT_IMPLEMENTED"))
    print(json.dumps({"version": "0.8.4", "scenarios": selected},
                     indent=2, ensure_ascii=False))
    print("V084_RECLAIM_MODEL_PASS hypothetical_cycle=" +
          str(any(r["cycle_present_in_model"] for r in selected)).upper() +
          " physical_kernel_swap=BLOCKED real_reclaim_tested=NO")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
