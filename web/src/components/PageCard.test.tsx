import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import { PageCard } from "./PageCard.tsx";

describe("PageCard", () => {
  it("renders the page number and fires onClick with the page", () => {
    const onClick = vi.fn();
    render(<PageCard page={7} onClick={onClick} />);
    const btn = screen.getByRole("button", { name: /p7/ });
    btn.click();
    expect(onClick).toHaveBeenCalledWith(7);
  });

  it("uses a custom label when provided", () => {
    render(<PageCard page={3} label="rightmost" onClick={() => {}} />);
    expect(screen.getByRole("button", { name: /rightmost/ })).toBeInTheDocument();
  });
});
