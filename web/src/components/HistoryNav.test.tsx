import { beforeEach, describe, expect, it } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import { HistoryNav } from "./HistoryNav.tsx";
import { useHistory } from "../state/historyStore.ts";
import type { NavEntry } from "../core/history.ts";

const E = (view: NavEntry["view"], label: string): NavEntry => ({ view, node: null, label });

beforeEach(() => {
  useHistory.setState({
    entries: [E("pages", "Pages"), E("tables", "Tables"), E("query", "Query")],
    index: 1, // current = Tables; one entry back, one forward
    applying: false,
  });
});

describe("HistoryNav split buttons", () => {
  it("enables Back and Forward when the current entry is in the middle", () => {
    render(<HistoryNav />);
    expect((screen.getByLabelText("back") as HTMLButtonElement).disabled).toBe(false);
    expect((screen.getByLabelText("forward") as HTMLButtonElement).disabled).toBe(false);
  });

  it("disables Back at the start of the stack", () => {
    useHistory.setState({ index: 0 });
    render(<HistoryNav />);
    expect((screen.getByLabelText("back") as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByLabelText("forward") as HTMLButtonElement).disabled).toBe(false);
  });

  it("Back dropdown lists prior entries and jumps on click", () => {
    render(<HistoryNav />);
    fireEvent.click(screen.getByLabelText("back history"));
    // index 1 → one entry back: "Pages"
    const item = screen.getByRole("button", { name: "Pages" });
    fireEvent.click(item);
    expect(useHistory.getState().index).toBe(0);
  });

  it("Forward dropdown lists later entries", () => {
    render(<HistoryNav />);
    fireEvent.click(screen.getByLabelText("forward history"));
    expect(screen.getByRole("button", { name: "Query" })).toBeInTheDocument();
  });
});
