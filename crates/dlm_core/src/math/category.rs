//! Exhaustive validation of finite categories/functors, not higher categories.
use super::{require, MathResult};
pub const MAX_MORPHISMS: usize = 64;
#[derive(Clone, Debug)]
pub struct Category {
    objects: usize,
    source: Vec<usize>,
    target: Vec<usize>,
    identities: Vec<usize>,
    /// compose[g * arrow_count + f] = g after f; None iff not composable.
    compose: Vec<Option<usize>>,
}
impl Category {
    pub fn new(
        objects: usize,
        source: Vec<usize>,
        target: Vec<usize>,
        identities: Vec<usize>,
        compose: Vec<Option<usize>>,
    ) -> MathResult<Self> {
        let n = source.len();
        require(
            objects <= MAX_MORPHISMS && n <= MAX_MORPHISMS,
            "category size budget exceeded",
        )?;
        require(
            target.len() == n && identities.len() == objects && compose.len() == n * n,
            "category table shape mismatch",
        )?;
        require(
            source.iter().chain(&target).all(|&o| o < objects),
            "unknown object",
        )?;
        require(identities.iter().all(|&i| i < n), "unknown identity")?;
        for (o, &i) in identities.iter().enumerate() {
            require(
                source[i] == o && target[i] == o,
                "identity has wrong endpoints",
            )?;
        }
        for g in 0..n {
            for f in 0..n {
                let c = compose[g * n + f];
                require(
                    c.is_some() == (source[g] == target[f]),
                    "composition domain mismatch",
                )?;
                if let Some(h) = c {
                    require(
                        h < n && source[h] == source[f] && target[h] == target[g],
                        "composition has wrong endpoints",
                    )?;
                }
            }
        }
        let out = Self {
            objects,
            source,
            target,
            identities,
            compose,
        };
        for f in 0..n {
            require(
                out.comp(out.identities[out.target[f]], f) == Some(f)
                    && out.comp(f, out.identities[out.source[f]]) == Some(f),
                "identity law fails",
            )?;
            for g in 0..n {
                for h in 0..n {
                    if let (Some(gf), Some(hg)) = (out.comp(g, f), out.comp(h, g)) {
                        require(out.comp(h, gf) == out.comp(hg, f), "associativity fails")?;
                    }
                }
            }
        }
        Ok(out)
    }
    pub fn objects(&self) -> usize {
        self.objects
    }
    pub fn arrows(&self) -> usize {
        self.source.len()
    }
    pub fn comp(&self, g: usize, f: usize) -> Option<usize> {
        if g >= self.arrows() || f >= self.arrows() {
            None
        } else {
            self.compose[g * self.arrows() + f]
        }
    }
    /// Check eta: F => G by all naturality squares. Functors are checked first.
    pub fn check_natural_transformation(
        &self,
        target: &Self,
        f_objects: &[usize],
        f_arrows: &[usize],
        g_objects: &[usize],
        g_arrows: &[usize],
        eta: &[usize],
    ) -> MathResult<()> {
        self.check_functor(target, f_objects, f_arrows)?;
        self.check_functor(target, g_objects, g_arrows)?;
        require(
            eta.len() == self.objects && eta.iter().all(|&a| a < target.arrows()),
            "natural transformation shape mismatch",
        )?;
        for (o, &a) in eta.iter().enumerate() {
            require(
                target.source[a] == f_objects[o] && target.target[a] == g_objects[o],
                "component endpoint mismatch",
            )?;
        }
        for a in 0..self.arrows() {
            require(
                target.comp(g_arrows[a], eta[self.source[a]])
                    == target.comp(eta[self.target[a]], f_arrows[a]),
                "naturality square fails",
            )?;
        }
        Ok(())
    }
    /// A functor is accepted only if endpoints, identities and all compositions agree.
    pub fn check_functor(
        &self,
        target: &Self,
        objects: &[usize],
        arrows: &[usize],
    ) -> MathResult<()> {
        require(
            objects.len() == self.objects && arrows.len() == self.arrows(),
            "functor shape mismatch",
        )?;
        require(
            objects.iter().all(|&o| o < target.objects)
                && arrows.iter().all(|&f| f < target.arrows()),
            "functor references unknown element",
        )?;
        for (f, &a) in arrows.iter().enumerate() {
            require(
                target.source[a] == objects[self.source[f]]
                    && target.target[a] == objects[self.target[f]],
                "functor endpoint mismatch",
            )?;
            for g in 0..self.arrows() {
                if let Some(h) = self.comp(g, f) {
                    require(
                        target.comp(arrows[g], a) == Some(arrows[h]),
                        "functor composition law fails",
                    )?;
                }
            }
        }
        for (o, &t) in objects.iter().enumerate() {
            require(
                arrows[self.identities[o]] == target.identities[t],
                "functor identity law fails",
            )?;
        }
        Ok(())
    }
}
