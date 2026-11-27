import math
import unittest

from benchmark import quality, reciprocal_rank_fusion


class RankingTests(unittest.TestCase):
    def test_quality_uses_all_judgments_and_first_relevant_rank(self):
        metrics, rank = quality([4, 2, 8], {2: 1, 6: 1})
        self.assertEqual(rank, 2)
        self.assertEqual(metrics["recall@10"], 0.5)
        self.assertAlmostEqual(metrics["ndcg@10"], 1 / (1 + math.log2(3)))
        self.assertEqual(metrics["mrr"], 0.5)

    def test_reciprocal_rank_is_not_cut_off_at_ten(self):
        metrics, _ = quality(list(range(11)), {10: 1})
        self.assertEqual(metrics["mrr"], 1 / 11)
        self.assertEqual(metrics["recall@10"], 0)
        self.assertEqual(quality([], {10: 1})[0]["mrr"], 0)

    def test_fusion_rewards_agreement_and_breaks_ties_by_row(self):
        self.assertEqual(reciprocal_rank_fusion([2, 8], [1, 8]), [8, 1, 2])
        self.assertNotIn(100, reciprocal_rank_fusion(list(range(101)), []))
