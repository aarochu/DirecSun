import { ChevronDown } from 'lucide-react';
import { useEffect, useId, useRef, useState } from 'react';
import type { CommandableMode, ManualInput } from '../hardware/deviceOperator';

interface ModeChoice {
  readonly label: string;
  readonly mode: CommandableMode;
  readonly input?: ManualInput;
}

export const MODE_CHOICES: readonly ModeChoice[] = [
  { label: 'Ready', mode: 'READY' },
  { label: 'Hold heading', mode: 'HOLD' },
  { label: 'Manual: rate', mode: 'MANUAL', input: 'rate' },
  { label: 'Manual: throttle', mode: 'MANUAL', input: 'throttle' },
  { label: 'Manual: wheel speed', mode: 'MANUAL', input: 'wheel' },
  { label: 'Detumble', mode: 'DETUMBLE' },
  { label: 'Sun point', mode: 'SUN_POINT' },
];

interface ModeMenuProps {
  readonly mode: string | null;
  readonly disabled: boolean;
  readonly onSelect: (mode: CommandableMode, input?: ManualInput) => void;
}

export function ModeMenu({ mode, disabled, onSelect }: ModeMenuProps) {
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
    const count = MODE_CHOICES.length;
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
        {MODE_CHOICES.map((choice, index) => (
          <li key={choice.label} role="none">
            <button
              type="button"
              role="menuitem"
              ref={(node) => {
                items.current[index] = node;
              }}
              className="modemenu__item"
              disabled={(choice.mode === 'HOLD' || choice.mode === 'MANUAL') && mode !== 'READY'}
              aria-current={choice.mode === mode && choice.input === undefined ? 'true' : undefined}
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
                onSelect(choice.mode, choice.input);
              }}
            >
              {choice.label}
            </button>
          </li>
        ))}
      </ul>
    </div>
  );
}
