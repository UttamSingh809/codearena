-- ============================================================
-- CodeArena — User XP and Leveling
-- ============================================================

BEGIN;

-- Add XP to users
ALTER TABLE users
    ADD COLUMN IF NOT EXISTS xp INTEGER NOT NULL DEFAULT 0;

-- Add a function to calculate XP based on difficulty
CREATE OR REPLACE FUNCTION calculate_xp(difficulty TEXT) RETURNS INTEGER AS $$
BEGIN
    IF difficulty = 'easy' THEN RETURN 10;
    ELSIF difficulty = 'medium' THEN RETURN 30;
    ELSIF difficulty = 'hard' THEN RETURN 60;
    ELSE RETURN 0;
    END IF;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

COMMENT ON COLUMN users.xp IS 'Total accumulated experience points';
COMMENT ON FUNCTION calculate_xp IS 'Returns XP value for a given problem difficulty';

COMMIT;