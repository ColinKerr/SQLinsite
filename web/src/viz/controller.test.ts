import { beforeEach, describe, expect, it, vi } from "vitest";
import { CanvasController } from "./controller.ts";
import { createVizStore, type VizStore } from "../state/store.ts";
import type { ObjectInfo } from "../core/types.ts";

// jsdom has no 2D canvas context; stub getContext so the controller constructs.
// (The constructor only stashes the context; these tests never trigger a paint —
// meta is null, so render() early-returns even if a rAF fires.)
function fakeCanvas(): HTMLCanvasElement {
  const c = document.createElement("canvas");
  c.getContext = (() => ({})) as unknown as HTMLCanvasElement["getContext"];
  return c;
}

function makeController(store: VizStore): CanvasController {
  return new CanvasController(
    fakeCanvas(), fakeCanvas(),
    document.createElement("div"), document.createElement("div"),
    store,
  );
}

const obj: ObjectInfo = {
  id: 7, type: "table", name: "T", tableName: "T",
  rootPage: 2, pageCount: 10, startPage: 2, startLeafPage: 3,
};

describe("CanvasController.navigateToObject", () => {
  beforeEach(() => {
    // rAF isn't needed for the assertions; make it a no-op so no stray render runs.
    vi.stubGlobal("requestAnimationFrame", () => 0 as unknown as number);
  });

  it("stays on the Blocks view when navigating to an object", async () => {
    const store = createVizStore();
    store.setState({ view: "blocks", objById: new Map([[obj.id, obj]]) });
    const c = makeController(store);

    await c.navigateToObject(obj.id);

    // Navigating to an object must scroll within the Blocks view, not switch to Pages.
    expect(store.getState().view).toBe("blocks");
  });

  it("stays on the Pages view when navigating to an object", async () => {
    const store = createVizStore();
    store.setState({ view: "pages", objById: new Map([[obj.id, obj]]) });
    const c = makeController(store);

    await c.navigateToObject(obj.id);

    expect(store.getState().view).toBe("pages");
  });

  it("logs an error for an unhandled view and changes nothing", async () => {
    const store = createVizStore();
    store.setState({ view: "tree", objById: new Map([[obj.id, obj]]) });
    const c = makeController(store);
    const err = vi.spyOn(console, "error").mockImplementation(() => {});

    await c.navigateToObject(obj.id);

    expect(err).toHaveBeenCalledWith("navigateToObject: unhandled view 'tree'");
    expect(store.getState().view).toBe("tree");
    err.mockRestore();
  });
});
