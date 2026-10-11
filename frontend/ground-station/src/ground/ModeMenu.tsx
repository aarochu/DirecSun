import { ChevronDown } from 'lucide-react';
import { useEffect, useId, useRef, useState } from 'react';

export interface MenuEntry {
  readonly label: string;
  readonly disabled: boolean;
  readonly current?: boolean;
  readonly tag?: string;
  readonly separated?: boolean;
  readonly onPick: () => void;
}

interface ModeMenuProps {
  readonly entries: readonly MenuEntry[];
  readonly disabled: boolean;
}

export function ModeMenu({ entries, disabled }: ModeMenuProps) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const trigger = useRef<HTMLButtonElement>(null);
  const items = useRef<(HTMLButtonElement | null)[]>([]);
  const menuId = useId();

  useEffect(() => {
    if (disabled) setOpen(false);
  }, [disabled]);

  useEffect(() => {
    if (!open) return;
    items.current.find((item) => item && !item.disabled)?.focus();
    const onPointer = (event: PointerEvent) => {
      if (!root.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', onPointer);
    return () => document.removeEventListener('pointerdown', onPointer);
  }, [open]);

  const move = (from: number, step: number) => {
    const count = entries.length;
    for (let i = 1; i <= count; i += 1) {
      const next = items.current[(from + step * i + count * i) % count];
      if (next && !next.disabled) return next.focus();
    }
  };

  return (
    <div className="modemenu" ref={root}>
      <button
        type="button"
        ref={trigger}
        className="button button--primary controls__direction controls__mode"
        aria-haspopup="menu"
        aria-expanded={open}
        aria-controls={menuId}
        aria-label="Set the box mode"
        disabled={disabled}
        onClick={() => setOpen((value) => !value)}
      >
        Mode
        <ChevronDown strokeWidth={1.75} aria-hidden />
      </button>
      <ul
        className="modemenu__list"
        id={menuId}
        role="menu"
        aria-label="Box modes"
        hidden={!open}
        onKeyDown={(event) => {
          if (event.key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            setOpen(false);
            trigger.current?.focus();
          }
        }}
      >
        {entries.map((choice, index) => (
          <li key={choice.label} role="none" className={choice.separated ? 'modemenu__separated' : undefined}>
            <button
              type="button"
              role="menuitem"
              ref={(node) => {
                items.current[index] = node;
              }}
              className="modemenu__item"
              disabled={choice.disabled}
              aria-label={choice.label}
              aria-current={choice.current ? 'true' : undefined}
              onKeyDown={(event) => {
                if (event.key === 'ArrowDown') {
                  event.preventDefault();
                  move(index, 1);
                } else if (event.key === 'ArrowUp') {
                  event.preventDefault();
                  move(index, -1);
                }
              }}
              onClick={() => {
                setOpen(false);
                trigger.current?.focus();
                choice.onPick();
              }}
            >
              {choice.label}
              {choice.tag ? <span className="modemenu__tag mono-label">{choice.tag}</span> : null}
            </button>
          </li>
        ))}
      </ul>
    </div>
  );
}
