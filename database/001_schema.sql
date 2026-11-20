-- Fish Tracker: reconstructed schema snapshot (October 2026)
-- Intended for provisioning a NEW Supabase database, not for execution
-- against the existing working project. This is not a migration.
-- Based on exported columns, constraints, and generated location expression.
-- IMPORTANT: Verify identity/sequence configuration for mycopy_fish_types.id,
-- PostGIS extension schema, and any indexes/triggers before fresh deployment.
-- Functions, grants, and RLS policies belong in later scripts.
-- Does NOT touch teammates' public.fish_types or public.fish_observations.

BEGIN;

CREATE SCHEMA IF NOT EXISTS extensions;
CREATE EXTENSION IF NOT EXISTS postgis WITH SCHEMA extensions;

CREATE TABLE public.users_data (
    user_id UUID PRIMARY KEY REFERENCES auth.users(id) ON DELETE CASCADE,
    username TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_active TIMESTAMPTZ
);

CREATE TABLE public.devices (
    device_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES public.users_data(user_id) ON DELETE CASCADE,
    device_name TEXT,
    registered_at TIMESTAMPTZ,
    last_upload_at TIMESTAMPTZ,
    is_active BOOLEAN NOT NULL DEFAULT true,
    registration_code_hash TEXT,
    secret_hash TEXT,
    claim_code_hash TEXT
);

CREATE TABLE public.device_settings (
    device_id UUID PRIMARY KEY REFERENCES public.devices(device_id) ON DELETE CASCADE,
    settings JSONB NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE public.mycopy_fish_types (
    -- The earlier schema used GENERATED ALWAYS AS IDENTITY.
    -- Verify that the live column is still an identity before treating
    -- this as a fully reproducible schema.
    id INTEGER GENERATED ALWAYS AS IDENTITY,
    species_of_fish TEXT NOT NULL,
    color_of_fish TEXT,
    created_by UUID,
    CONSTRAINT fish_types_primarykey PRIMARY KEY (id),
    CONSTRAINT fish_types_namekey UNIQUE (species_of_fish)
);

CREATE TABLE public.mycopy_fish_observations (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    fish_type_id INTEGER NOT NULL
        REFERENCES public.mycopy_fish_types(id),
    caught_at TIMESTAMPTZ NOT NULL,
    latitude DOUBLE PRECISION NOT NULL,
    longitude DOUBLE PRECISION NOT NULL,
    location extensions.geography(Point, 4326)
        GENERATED ALWAYS AS (
            CASE
                WHEN latitude IS NOT NULL AND longitude IS NOT NULL
                THEN extensions.ST_SetSRID(
                    extensions.ST_MakePoint(longitude, latitude), 4326
                )::extensions.geography
                ELSE NULL::extensions.geography
            END
        ) STORED,
    device_id TEXT,
    user_id UUID DEFAULT auth.uid()
        REFERENCES public.users_data(user_id) ON DELETE SET NULL,
    registered_device_id UUID
        REFERENCES public.devices(device_id),
    CONSTRAINT coordinates_together CHECK (
        (latitude IS NULL AND longitude IS NULL)
        OR (latitude IS NOT NULL AND longitude IS NOT NULL)
    ),
    CONSTRAINT valid_latitude CHECK (latitude BETWEEN -90 AND 90),
    CONSTRAINT valid_longitude CHECK (longitude BETWEEN -180 AND 180)
);

CREATE TABLE public.claim_attempts (
    user_id UUID NOT NULL,
    attempted_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- RLS is enabled in the working project. Policies and API grants
-- must be restored separately; enabling RLS without policies denies
-- ordinary client access by default.
ALTER TABLE public.users_data ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.mycopy_fish_types ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.mycopy_fish_observations ENABLE ROW LEVEL SECURITY;

-- Security status for device_settings and claim_attempts has not yet
-- been exported. Do not assume their live RLS configuration.

COMMIT;
