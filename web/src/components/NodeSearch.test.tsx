import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { NodeSearch } from "./NodeSearch.tsx";
import { useTree } from "../state/treeStore.ts";

function jsonResp(body: unknown) {
  return { ok: true, json: async () => body } as Response;
}

const reveal = vi.fn();

beforeEach(() => {
  reveal.mockReset();
  useTree.setState({ revealPage: reveal as unknown as (p: number) => Promise<void> });
  vi.stubGlobal("fetch", vi.fn(async (url: string) => {
    if (String(url).includes("/api/tree/search")) {
      return jsonResp({ matches: [
        { page: 123, label: "Page 123", pageType: "table-leaf", objectId: 1, hasChildren: 0, subtreePageCount: 1 },
        { page: 1230, label: "Page 1230", pageType: "table-leaf", objectId: 1, hasChildren: 0, subtreePageCount: 1 },
      ] });
    }
    return jsonResp({});
  }));
});
afterEach(() => vi.restoreAllMocks());

const type = (v: string) =>
  fireEvent.change(screen.getByLabelText("Find page by number"), { target: { value: v } });

describe("NodeSearch", () => {
  it("shows no dropdown until a digit is entered", async () => {
    render(<NodeSearch pageSize={4096} />);
    type("ab"); // no digits
    // Give any (cancelled) fetch a chance; the dropdown must not appear.
    await new Promise((r) => setTimeout(r, 200));
    expect(screen.queryByRole("listbox")).toBeNull();
  });

  it("lists matches after 3 digits and jumps to the top match on Enter", async () => {
    render(<NodeSearch pageSize={4096} />);
    type("123");
    expect(await screen.findByText("Page 123")).toBeInTheDocument();
    expect(screen.getByText("Page 1230")).toBeInTheDocument();

    fireEvent.keyDown(screen.getByLabelText("Find page by number"), { key: "Enter" });
    expect(reveal).toHaveBeenCalledWith(123); // top match selected by default
  });

  it("arrow keys move the selection before Enter", async () => {
    render(<NodeSearch pageSize={4096} />);
    type("123");
    await screen.findByText("Page 123");
    const input = screen.getByLabelText("Find page by number");
    fireEvent.keyDown(input, { key: "ArrowDown" });
    fireEvent.keyDown(input, { key: "Enter" });
    expect(reveal).toHaveBeenCalledWith(1230);
  });

  it("clicking a match reveals it and closes the dropdown", async () => {
    render(<NodeSearch pageSize={4096} />);
    type("123");
    fireEvent.mouseDown(await screen.findByText("Page 1230"));
    expect(reveal).toHaveBeenCalledWith(1230);
    await waitFor(() => expect(screen.queryByRole("listbox")).toBeNull());
  });
});
