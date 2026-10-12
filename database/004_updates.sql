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
