import { useEffect } from "react";
import { create } from "zustand";
import type { TreeNode } from "../core/treeModel.ts";

// The behavior a view wants when a b-tree tree node is activated (body-clicked).
// The shared, view-agnostic BTreeTree calls the handler the active view has
// registered; it holds no per-view logic itself.
export type NodeActivate = (node: TreeNode) => void;

const NOOP: NodeActivate = () => {};

interface TreeSelectionState {
  activate: NodeActivate;
  setActivate(fn: NodeActivate): void;
}

export const useTreeSelection = create<TreeSelectionState>((set) => ({
  activate: NOOP,
  setActivate: (fn) => set({ activate: fn }),
}));

// A view calls this to inject its node-activation handler for as long as it is
// mounted. The single shared BTreeTree instance invokes whichever handler is
// currently registered, so switching views swaps the behavior without the tree
// knowing anything about the views. `handler` should be memoized (useCallback).
export function useRegisterNodeActivation(handler: NodeActivate): void {
  const setActivate = useTreeSelection((s) => s.setActivate);
  useEffect(() => {
    setActivate(handler);
    return () => setActivate(NOOP);
  }, [handler, setActivate]);
}
