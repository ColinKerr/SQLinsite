import { createContext, useContext, useState, type ReactNode } from "react";
import type { CanvasController } from "../viz/controller.ts";

interface Ctx {
  controller: CanvasController | null;
  setController: (c: CanvasController | null) => void;
}

const ControllerCtx = createContext<Ctx>({ controller: null, setController: () => {} });

export function ControllerProvider({ children }: { children: ReactNode }) {
  const [controller, setController] = useState<CanvasController | null>(null);
  return (
    <ControllerCtx.Provider value={{ controller, setController }}>
      {children}
    </ControllerCtx.Provider>
  );
}

export const useControllerCtx = () => useContext(ControllerCtx);
export const useController = () => useContext(ControllerCtx).controller;
