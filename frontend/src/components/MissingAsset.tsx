import { Box, ImageOff } from 'lucide-react';

interface MissingAssetProps {
  label: string;
  part: string;
  icon?: 'cad' | 'photo';
}

export function MissingAsset({ label, part, icon = 'photo' }: MissingAssetProps) {
  const Icon = icon === 'cad' ? Box : ImageOff;
  return (
    <div className="missing-asset" role="img" aria-label={`${label}: ${part}`}>
      <Icon className="missing-asset__icon" strokeWidth={1.75} aria-hidden />
      <span className="missing-asset__label">{label}</span>
      <span className="missing-asset__part">{part}</span>
    </div>
  );
}
