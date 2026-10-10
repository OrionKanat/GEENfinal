BEGIN;

CREATE SCHEMA IF NOT EXISTS extensions;

CREATE EXTENSION IF NOT EXISTS postgis
    WITH SCHEMA extensions;

CREATE TABLE public.mycopy_fish_types (
    id INTEGER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    species_of_fish TEXT NOT NULL UNIQUE,
    color_of_fish TEXT
);

CREATE TABLE public.mycopy_fish_observations (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),

    fish_type_id INTEGER NOT NULL
        REFERENCES public.mycopy_fish_types(id),

    caught_at TIMESTAMPTZ NOT NULL,

    uploaded_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,

    device_record_id TEXT NOT NULL UNIQUE,

    location extensions.geography(Point, 4326)
        GENERATED ALWAYS AS (
            CASE
                WHEN latitude IS NOT NULL
                 AND longitude IS NOT NULL
                THEN extensions.ST_SetSRID(
                    extensions.ST_MakePoint(
                        longitude,
                        latitude
                    ),
                    4326
                )::extensions.geography
                ELSE NULL
            END
        ) STORED,

    CONSTRAINT valid_latitude
        CHECK (latitude BETWEEN -90 AND 90),

    CONSTRAINT valid_longitude
        CHECK (longitude BETWEEN -180 AND 180),

    CONSTRAINT coordinates_together
        CHECK (
            (latitude IS NULL AND longitude IS NULL)
            OR
            (latitude IS NOT NULL AND longitude IS NOT NULL)
        ),

    CONSTRAINT device_record_id_not_blank
        CHECK (length(trim(device_record_id)) > 0)
);

ALTER TABLE public.mycopy_fish_types
    ENABLE ROW LEVEL SECURITY;

ALTER TABLE public.mycopy_fish_observations
    ENABLE ROW LEVEL SECURITY;

COMMIT;
