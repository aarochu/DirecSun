import { useReveal } from '../lib/useReveal';

interface SectionHeadingProps {
  index: string;
  title: string;
  titleId: string;
  intro?: string;
}

export function SectionHeading({ index, title, titleId, intro }: SectionHeadingProps) {
  const ref = useReveal<HTMLDivElement>();
  return (
    <div className="section-heading" ref={ref}>
      <p className="section-heading__index mono-label">{index}</p>
      <h2 className="section-heading__title" id={titleId}>
        {title}
      </h2>
      {intro && <p className="section-heading__intro">{intro}</p>}
    </div>
  );
}
