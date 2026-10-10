BEGIN;

INSERT INTO public.mycopy_fish_types
    (id, species_of_fish, color_of_fish)
OVERRIDING SYSTEM VALUE
VALUES
    (1, 'Brown Trout', 'Brown'),
    (2, 'Rainbow Trout', 'Rainbow'),
    (3, 'Brook Trout', 'Brook'),
    (4, 'Cutthroat Trout', 'Cutthroat');

-- Move the automatic ID sequence past the seeded IDs.
SELECT setval(
    pg_get_serial_sequence(
        'public.mycopy_fish_types',
        'id'
    ),
    (SELECT MAX(id) FROM public.mycopy_fish_types),
    true
);

COMMIT;
