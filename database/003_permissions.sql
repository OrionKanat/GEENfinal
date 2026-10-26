-- 003_permissions.sql
-- GitHub draft reconstructed from Supabase exports on 2026-10-25.
-- Intended for a FRESH installation after schema and function creation.
-- DO NOT execute on the working Supabase project without reviewing dependencies.
-- Does not modify public.fish_types or public.fish_observations.

BEGIN;

-- All six project tables have RLS enabled in the current database.
ALTER TABLE public.users_data ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.device_settings ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.mycopy_fish_types ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.mycopy_fish_observations ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.claim_attempts ENABLE ROW LEVEL SECURITY;

-- Remove prototype policies that previously exposed catches or permitted inserts.
DROP POLICY IF EXISTS prototype_read_fish_types ON public.mycopy_fish_types;
DROP POLICY IF EXISTS prototype_read_observations ON public.mycopy_fish_observations;
DROP POLICY IF EXISTS prototype_insert_observations ON public.mycopy_fish_observations;
DROP POLICY IF EXISTS read_types ON public.mycopy_fish_types;
DROP POLICY IF EXISTS read_observations ON public.mycopy_fish_observations;
DROP POLICY IF EXISTS device_insert ON public.mycopy_fish_observations;
DROP POLICY IF EXISTS testing_read_types ON public.mycopy_fish_types;
DROP POLICY IF EXISTS testing_read_observations ON public.mycopy_fish_observations;
DROP POLICY IF EXISTS testing_insert_observations ON public.mycopy_fish_observations;

-- Recreate the four verified policies exactly in purpose.
DROP POLICY IF EXISTS "Users view own profile" ON public.users_data;
CREATE POLICY "Users view own profile" ON public.users_data
  FOR SELECT TO authenticated USING ((SELECT auth.uid()) = user_id);

DROP POLICY IF EXISTS "Users view own devices" ON public.devices;
CREATE POLICY "Users view own devices" ON public.devices
  FOR SELECT TO authenticated USING ((SELECT auth.uid()) = user_id);

DROP POLICY IF EXISTS "Read own observations" ON public.mycopy_fish_observations;
CREATE POLICY "Read own observations" ON public.mycopy_fish_observations
  FOR SELECT TO authenticated USING (user_id = (SELECT auth.uid()));

DROP POLICY IF EXISTS "Read standard and own fish types" ON public.mycopy_fish_types;
CREATE POLICY "Read standard and own fish types" ON public.mycopy_fish_types
  FOR SELECT TO authenticated
  USING (created_by IS NULL OR created_by = (SELECT auth.uid()));

-- The export reported no policies on device_settings or claim_attempts.
-- With RLS enabled and no policies, ordinary API roles cannot access rows.

-- Remove broad table privileges, especially those inherited from defaults.
REVOKE ALL PRIVILEGES ON TABLE
  public.users_data, public.devices, public.device_settings,
  public.mycopy_fish_types, public.mycopy_fish_observations,
  public.claim_attempts
FROM anon, authenticated;

-- Confirmed SELECT privileges. Anonymous SELECT grants on the fish tables
-- match the live export, but RLS currently has no matching anon policy.
GRANT SELECT ON TABLE public.users_data, public.devices TO authenticated;
GRANT SELECT ON TABLE public.mycopy_fish_types,
  public.mycopy_fish_observations TO anon, authenticated;

-- These functions MUST exist before this block is executed.
-- PUBLIC may otherwise inherit PostgreSQL's default EXECUTE privilege.
REVOKE ALL ON FUNCTION public.claim_device(text) FROM PUBLIC, anon, authenticated;
GRANT EXECUTE ON FUNCTION public.claim_device(text) TO authenticated;

REVOKE ALL ON FUNCTION public.provision_device(text) FROM PUBLIC, anon, authenticated;
-- Provisioning is administrator-only; no API-role grant.

REVOKE ALL ON FUNCTION public.ingest_catch(
  uuid, text, integer, timestamptz, double precision, double precision
) FROM PUBLIC, anon, authenticated;
GRANT EXECUTE ON FUNCTION public.ingest_catch(
  uuid, text, integer, timestamptz, double precision, double precision
) TO anon, authenticated;

COMMIT;
