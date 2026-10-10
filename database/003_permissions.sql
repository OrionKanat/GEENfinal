BEGIN;

REVOKE ALL ON TABLE
    public.mycopy_fish_types,
    public.mycopy_fish_observations
FROM anon, authenticated;

GRANT USAGE ON SCHEMA public TO authenticated;

GRANT SELECT ON TABLE
    public.mycopy_fish_types,
    public.mycopy_fish_observations
TO authenticated;

GRANT INSERT (
    fish_type_id,
    caught_at,
    latitude,
    longitude,
    device_record_id
)
ON public.mycopy_fish_observations
TO authenticated;

CREATE POLICY prototype_read_fish_types
ON public.mycopy_fish_types
FOR SELECT
TO authenticated
USING (
    (SELECT auth.uid()) = 'YOUR_TEST_USER_UID'::uuid
);

CREATE POLICY prototype_read_observations
ON public.mycopy_fish_observations
FOR SELECT
TO authenticated
USING (
    (SELECT auth.uid()) = 'YOUR_TEST_USER_UID'::uuid
);

CREATE POLICY prototype_insert_observations
ON public.mycopy_fish_observations
FOR INSERT
TO authenticated
WITH CHECK (
    (SELECT auth.uid()) = 'YOUR_TEST_USER_UID'::uuid
);

COMMIT;
