BEGIN;

-- Remove the three prototype test catches created without GPS.
DELETE FROM public.mycopy_fish_observations
WHERE device_record_id IN (
    'test-catch-001',
    'test-batch-003',
    'test-api-002'
);

-- Require coordinates for every future catch.
ALTER TABLE public.mycopy_fish_observations
    ALTER COLUMN latitude SET NOT NULL,
    ALTER COLUMN longitude SET NOT NULL;

COMMIT;


-- Upload history is not required for this prototype.
-- Preserve caught_at as the original catch timestamp.
ALTER TABLE public.mycopy_fish_observations
    DROP COLUMN uploaded_at;



-- Permission updates 

BEGIN;

ALTER TABLE public.mycopy_fish_types
    ENABLE ROW LEVEL SECURITY;

ALTER TABLE public.mycopy_fish_observations
    ENABLE ROW LEVEL SECURITY;

-- Remove the policies from our previous prototype setup.
DROP POLICY IF EXISTS prototype_read_fish_types
    ON public.mycopy_fish_types;

DROP POLICY IF EXISTS prototype_read_observations
    ON public.mycopy_fish_observations;

DROP POLICY IF EXISTS prototype_insert_observations
    ON public.mycopy_fish_observations;

-- Remove the policies from the alternative proposed setup,
-- if that setup was already applied.
DROP POLICY IF EXISTS read_types
    ON public.mycopy_fish_types;

DROP POLICY IF EXISTS read_observations
    ON public.mycopy_fish_observations;

DROP POLICY IF EXISTS device_insert
    ON public.mycopy_fish_observations;

-- Make this script rerunnable.
DROP POLICY IF EXISTS testing_read_types
    ON public.mycopy_fish_types;

DROP POLICY IF EXISTS testing_read_observations
    ON public.mycopy_fish_observations;

DROP POLICY IF EXISTS testing_insert_observations
    ON public.mycopy_fish_observations;

REVOKE ALL ON TABLE
    public.mycopy_fish_types,
    public.mycopy_fish_observations
FROM anon, authenticated;

GRANT USAGE ON SCHEMA public TO anon, authenticated;

GRANT SELECT ON TABLE
    public.mycopy_fish_types,
    public.mycopy_fish_observations
TO anon, authenticated;

GRANT INSERT (
    fish_type_id,
    caught_at,
    latitude,
    longitude,
    device_record_id
)
ON public.mycopy_fish_observations
TO anon, authenticated;

CREATE POLICY testing_read_types
ON public.mycopy_fish_types
FOR SELECT
TO anon, authenticated
USING (true);

CREATE POLICY testing_read_observations
ON public.mycopy_fish_observations
FOR SELECT
TO anon, authenticated
USING (true);

CREATE POLICY testing_insert_observations
ON public.mycopy_fish_observations
FOR INSERT
TO anon, authenticated
WITH CHECK (true);

COMMIT;
