//! Bounded finite algebra by exhaustive law checking. Tables are region data;
//! accepted objects are runtime witnesses, not StaticProof or new axioms.
use super::category::Category;
use super::{require, MathError, MathResult};
use std::collections::BTreeSet;
pub const MAX_ORDER: usize = 64;
fn order(n: usize) -> MathResult<()> {
    require(
        n > 0 && n <= MAX_ORDER,
        "finite algebra order must be 1..=64",
    )
}
fn table(n: usize, t: &[usize]) -> MathResult<()> {
    require(t.len() == n * n, "operation table shape mismatch")?;
    require(
        t.iter().all(|&x| x < n),
        "operation table element out of range",
    )
}
fn element(n: usize, x: usize) -> MathResult<()> {
    require(x < n, "algebra element out of range")
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FiniteGroup {
    n: usize,
    unit: usize,
    operation: Vec<usize>,
    inverse: Vec<usize>,
}
impl FiniteGroup {
    pub fn new(n: usize, unit: usize, operation: Vec<usize>) -> MathResult<Self> {
        order(n)?;
        element(n, unit)?;
        table(n, &operation)?;
        let op = |a: usize, b: usize| operation[a * n + b];
        for a in 0..n {
            require(
                op(unit, a) == a && op(a, unit) == a,
                "group identity law fails",
            )?;
            for b in 0..n {
                for c in 0..n {
                    require(
                        op(op(a, b), c) == op(a, op(b, c)),
                        "group associativity fails",
                    )?;
                }
            }
        }
        let mut inverse = Vec::new();
        for a in 0..n {
            inverse.push(
                (0..n)
                    .find(|&b| op(a, b) == unit && op(b, a) == unit)
                    .ok_or_else(|| MathError("group inverse law fails".into()))?,
            );
        }
        Ok(Self {
            n,
            unit,
            operation,
            inverse,
        })
    }
    pub fn cyclic(n: usize) -> MathResult<Self> {
        order(n)?;
        Self::new(n, 0, (0..n * n).map(|i| (i / n + i % n) % n).collect())
    }
    pub fn order(&self) -> usize {
        self.n
    }
    pub fn identity(&self) -> usize {
        self.unit
    }
    pub fn table(&self) -> &[usize] {
        &self.operation
    }
    fn op(&self, a: usize, b: usize) -> usize {
        self.operation[a * self.n + b]
    }
    pub fn multiply(&self, a: usize, b: usize) -> MathResult<usize> {
        element(self.n, a)?;
        element(self.n, b)?;
        Ok(self.op(a, b))
    }
    pub fn inverse(&self, a: usize) -> MathResult<usize> {
        element(self.n, a)?;
        Ok(self.inverse[a])
    }
    pub fn is_abelian(&self) -> bool {
        (0..self.n).all(|a| (0..self.n).all(|b| self.op(a, b) == self.op(b, a)))
    }
    pub fn subgroup(&self, members: &[usize]) -> MathResult<Subgroup> {
        require(
            !members.is_empty() && members.len() <= self.n,
            "invalid subgroup size",
        )?;
        let set: BTreeSet<_> = members.iter().copied().collect();
        require(
            set.len() == members.len() && set.iter().all(|&x| x < self.n),
            "invalid or duplicate subgroup element",
        )?;
        require(set.contains(&self.unit), "subgroup omits identity")?;
        for &a in &set {
            require(
                set.contains(&self.inverse[a]),
                "subgroup not closed under inverse",
            )?;
            for &b in &set {
                require(
                    set.contains(&self.op(a, b)),
                    "subgroup not closed under product",
                )?;
            }
        }
        let embedding: Vec<_> = set.iter().copied().collect();
        let pos = |x| embedding.binary_search(&x).unwrap();
        let k = embedding.len();
        let mut operations = Vec::new();
        for &a in &embedding {
            for &b in &embedding {
                operations.push(pos(self.op(a, b)));
            }
        }
        let group = Self::new(k, pos(self.unit), operations)?;
        let normal = (0..self.n).all(|g| {
            embedding
                .iter()
                .all(|&h| set.contains(&self.op(self.op(g, h), self.inverse[g])))
        });
        Ok(Subgroup {
            ambient: self.clone(),
            group,
            embedding,
            normal,
        })
    }
    pub fn quotient(&self, members: &[usize]) -> MathResult<QuotientGroup> {
        let sub = self.subgroup(members)?;
        require(sub.normal, "quotient requires a normal subgroup")?;
        let mut projection = vec![usize::MAX; self.n];
        let mut cosets = Vec::new();
        for g in 0..self.n {
            if projection[g] != usize::MAX {
                continue;
            }
            let coset: BTreeSet<_> = sub.embedding.iter().map(|&h| self.op(g, h)).collect();
            let id = cosets.len();
            for &x in &coset {
                projection[x] = id;
            }
            cosets.push(coset.into_iter().collect::<Vec<_>>());
        }
        let k = cosets.len();
        let mut operations = Vec::new();
        for a in &cosets {
            for b in &cosets {
                operations.push(projection[self.op(a[0], b[0])]);
            }
        }
        let group = Self::new(k, projection[self.unit], operations)?;
        Ok(QuotientGroup {
            group,
            projection,
            cosets,
        })
    }
    pub fn homomorphism(&self, target: &Self, mapping: &[usize]) -> MathResult<GroupHomomorphism> {
        require(
            mapping.len() == self.n && mapping.iter().all(|&x| x < target.n),
            "group map shape or range mismatch",
        )?;
        require(
            mapping[self.unit] == target.unit,
            "group map does not preserve identity",
        )?;
        for a in 0..self.n {
            for b in 0..self.n {
                require(
                    mapping[self.op(a, b)] == target.op(mapping[a], mapping[b]),
                    "group map product law fails",
                )?;
            }
        }
        let kernel = self.subgroup(
            &(0..self.n)
                .filter(|&a| mapping[a] == target.unit)
                .collect::<Vec<_>>(),
        )?;
        let image = target.subgroup(
            &mapping
                .iter()
                .copied()
                .collect::<BTreeSet<_>>()
                .into_iter()
                .collect::<Vec<_>>(),
        )?;
        Ok(GroupHomomorphism {
            source: self.clone(),
            target: target.clone(),
            mapping: mapping.to_vec(),
            kernel,
            image,
        })
    }
    /// Group as a one-object category; every arrow has an inverse.
    pub fn as_category(&self) -> MathResult<Category> {
        Category::new(
            1,
            vec![0; self.n],
            vec![0; self.n],
            vec![self.unit],
            self.operation.iter().copied().map(Some).collect(),
        )
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Subgroup {
    ambient: FiniteGroup,
    group: FiniteGroup,
    embedding: Vec<usize>,
    normal: bool,
}
impl Subgroup {
    pub fn ambient(&self) -> &FiniteGroup {
        &self.ambient
    }
    pub fn group(&self) -> &FiniteGroup {
        &self.group
    }
    pub fn embedding(&self) -> &[usize] {
        &self.embedding
    }
    pub fn is_normal(&self) -> bool {
        self.normal
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct QuotientGroup {
    group: FiniteGroup,
    projection: Vec<usize>,
    cosets: Vec<Vec<usize>>,
}
impl QuotientGroup {
    pub fn group(&self) -> &FiniteGroup {
        &self.group
    }
    pub fn projection(&self) -> &[usize] {
        &self.projection
    }
    pub fn cosets(&self) -> &[Vec<usize>] {
        &self.cosets
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GroupHomomorphism {
    source: FiniteGroup,
    target: FiniteGroup,
    mapping: Vec<usize>,
    kernel: Subgroup,
    image: Subgroup,
}
impl GroupHomomorphism {
    pub fn mapping(&self) -> &[usize] {
        &self.mapping
    }
    pub fn kernel(&self) -> &Subgroup {
        &self.kernel
    }
    pub fn image(&self) -> &Subgroup {
        &self.image
    }
    pub fn is_injective(&self) -> bool {
        self.kernel.group.order() == 1
    }
    pub fn is_surjective(&self) -> bool {
        self.image.group.order() == self.target.order()
    }
    /// Construct and recheck G/ker(f) -> im(f), with explicit relabeling.
    pub fn first_isomorphism(&self) -> MathResult<FirstIsomorphism> {
        let quotient = self.source.quotient(&self.kernel.embedding)?;
        let induced: Vec<_> = quotient
            .cosets
            .iter()
            .map(|c| {
                self.image
                    .embedding
                    .binary_search(&self.mapping[c[0]])
                    .unwrap()
            })
            .collect();
        let checked = quotient.group.homomorphism(&self.image.group, &induced)?;
        require(
            checked.is_injective() && checked.is_surjective(),
            "induced map is not an isomorphism",
        )?;
        Ok(FirstIsomorphism {
            quotient,
            image: self.image.clone(),
            induced,
        })
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FirstIsomorphism {
    quotient: QuotientGroup,
    image: Subgroup,
    induced: Vec<usize>,
}

impl FirstIsomorphism {
    pub fn quotient(&self) -> &QuotientGroup {
        &self.quotient
    }
    pub fn image(&self) -> &Subgroup {
        &self.image
    }
    pub fn induced(&self) -> &[usize] {
        &self.induced
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FiniteRing {
    additive: FiniteGroup,
    one: usize,
    multiplication: Vec<usize>,
}
impl FiniteRing {
    /// Unital rings, including the one-element zero ring; multiplication may be noncommutative.
    pub fn new(
        n: usize,
        zero: usize,
        one: usize,
        addition: Vec<usize>,
        multiplication: Vec<usize>,
    ) -> MathResult<Self> {
        let additive = FiniteGroup::new(n, zero, addition)?;
        require(additive.is_abelian(), "ring addition must be abelian")?;
        element(n, one)?;
        table(n, &multiplication)?;
        let ring = Self {
            additive,
            one,
            multiplication,
        };
        for a in 0..n {
            require(
                ring.times(one, a) == a && ring.times(a, one) == a,
                "ring unit law fails",
            )?;
            require(
                ring.times(zero, a) == zero && ring.times(a, zero) == zero,
                "ring zero law fails",
            )?;
            for b in 0..n {
                for c in 0..n {
                    require(
                        ring.times(ring.times(a, b), c) == ring.times(a, ring.times(b, c)),
                        "ring multiplication associativity fails",
                    )?;
                    require(
                        ring.times(a, ring.plus(b, c))
                            == ring.plus(ring.times(a, b), ring.times(a, c)),
                        "left distributivity fails",
                    )?;
                    require(
                        ring.times(ring.plus(a, b), c)
                            == ring.plus(ring.times(a, c), ring.times(b, c)),
                        "right distributivity fails",
                    )?;
                }
            }
        }
        Ok(ring)
    }
    pub fn residues(n: usize) -> MathResult<Self> {
        order(n)?;
        Self::new(
            n,
            0,
            1 % n,
            (0..n * n).map(|i| (i / n + i % n) % n).collect(),
            (0..n * n).map(|i| ((i / n) * (i % n)) % n).collect(),
        )
    }
    pub fn order(&self) -> usize {
        self.additive.n
    }
    pub fn zero(&self) -> usize {
        self.additive.unit
    }
    pub fn one(&self) -> usize {
        self.one
    }
    pub fn additive(&self) -> &FiniteGroup {
        &self.additive
    }
    pub fn addition_table(&self) -> &[usize] {
        &self.additive.operation
    }
    pub fn multiplication_table(&self) -> &[usize] {
        &self.multiplication
    }
    fn plus(&self, a: usize, b: usize) -> usize {
        self.additive.op(a, b)
    }
    fn times(&self, a: usize, b: usize) -> usize {
        self.multiplication[a * self.order() + b]
    }
    pub fn add(&self, a: usize, b: usize) -> MathResult<usize> {
        element(self.order(), a)?;
        element(self.order(), b)?;
        Ok(self.plus(a, b))
    }
    pub fn multiply(&self, a: usize, b: usize) -> MathResult<usize> {
        element(self.order(), a)?;
        element(self.order(), b)?;
        Ok(self.times(a, b))
    }
    pub fn negate(&self, a: usize) -> MathResult<usize> {
        self.additive.inverse(a)
    }
    pub fn is_commutative(&self) -> bool {
        (0..self.order()).all(|a| (0..self.order()).all(|b| self.times(a, b) == self.times(b, a)))
    }
    pub fn field(&self) -> MathResult<FiniteField> {
        require(self.zero() != self.one, "zero ring is not a field")?;
        require(
            self.is_commutative(),
            "field multiplication must be commutative",
        )?;
        let mut inverse = vec![usize::MAX; self.order()];
        for (a, slot) in inverse.iter_mut().enumerate() {
            if a != self.zero() {
                *slot = (0..self.order())
                    .find(|&b| self.times(a, b) == self.one && self.times(b, a) == self.one)
                    .ok_or_else(|| MathError("nonzero field element has no inverse".into()))?;
            }
        }
        Ok(FiniteField {
            ring: self.clone(),
            inverse,
        })
    }
    pub fn homomorphism(&self, target: &Self, mapping: &[usize]) -> MathResult<RingHomomorphism> {
        let additive = self.additive.homomorphism(&target.additive, mapping)?;
        require(
            mapping[self.one] == target.one,
            "ring map must preserve one",
        )?;
        for a in 0..self.order() {
            for b in 0..self.order() {
                require(
                    mapping[self.times(a, b)] == target.times(mapping[a], mapping[b]),
                    "ring map multiplication law fails",
                )?;
            }
        }
        Ok(RingHomomorphism { additive })
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FiniteField {
    ring: FiniteRing,
    inverse: Vec<usize>,
}
impl FiniteField {
    pub fn prime(p: usize) -> MathResult<Self> {
        FiniteRing::residues(p)?.field()
    }
    pub fn ring(&self) -> &FiniteRing {
        &self.ring
    }
    pub fn inverse(&self, a: usize) -> MathResult<usize> {
        element(self.ring.order(), a)?;
        require(a != self.ring.zero(), "zero has no multiplicative inverse")?;
        Ok(self.inverse[a])
    }
    pub fn divide(&self, a: usize, b: usize) -> MathResult<usize> {
        self.ring.multiply(a, self.inverse(b)?)
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RingHomomorphism {
    additive: GroupHomomorphism,
}
impl RingHomomorphism {
    pub fn mapping(&self) -> &[usize] {
        self.additive.mapping()
    }
    pub fn kernel(&self) -> &Subgroup {
        self.additive.kernel()
    }
    pub fn image(&self) -> &Subgroup {
        self.additive.image()
    }
    pub fn is_injective(&self) -> bool {
        self.additive.is_injective()
    }
    pub fn is_surjective(&self) -> bool {
        self.additive.is_surjective()
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FiniteModule {
    ring: FiniteRing,
    additive: FiniteGroup,
    action: Vec<usize>,
}
impl FiniteModule {
    /// Finite unital left module. action[r * module_order + x] = r.x.
    pub fn new(ring: FiniteRing, additive: FiniteGroup, action: Vec<usize>) -> MathResult<Self> {
        require(additive.is_abelian(), "module addition must be abelian")?;
        let n = additive.n;
        let rn = ring.order();
        require(
            action.len() == rn * n && action.iter().all(|&x| x < n),
            "module action shape or range mismatch",
        )?;
        let m = Self {
            ring,
            additive,
            action,
        };
        for x in 0..n {
            require(
                m.act(m.ring.one(), x) == x && m.act(m.ring.zero(), x) == m.additive.unit,
                "module unit/zero scalar law fails",
            )?;
        }
        for r in 0..rn {
            require(
                m.act(r, m.additive.unit) == m.additive.unit,
                "module zero vector law fails",
            )?;
            for x in 0..n {
                for y in 0..n {
                    require(
                        m.act(r, m.additive.op(x, y)) == m.additive.op(m.act(r, x), m.act(r, y)),
                        "module vector distributivity fails",
                    )?;
                }
            }
            for s in 0..rn {
                for x in 0..n {
                    require(
                        m.act(m.ring.plus(r, s), x) == m.additive.op(m.act(r, x), m.act(s, x)),
                        "module scalar distributivity fails",
                    )?;
                    require(
                        m.act(m.ring.times(r, s), x) == m.act(r, m.act(s, x)),
                        "module scalar associativity fails",
                    )?;
                }
            }
        }
        Ok(m)
    }
    pub fn regular(ring: &FiniteRing) -> MathResult<Self> {
        Self::new(
            ring.clone(),
            ring.additive.clone(),
            ring.multiplication.clone(),
        )
    }
    pub fn ring(&self) -> &FiniteRing {
        &self.ring
    }
    pub fn additive(&self) -> &FiniteGroup {
        &self.additive
    }
    pub fn action_table(&self) -> &[usize] {
        &self.action
    }
    fn act(&self, r: usize, x: usize) -> usize {
        self.action[r * self.additive.n + x]
    }
    pub fn scale(&self, r: usize, x: usize) -> MathResult<usize> {
        element(self.ring.order(), r)?;
        element(self.additive.n, x)?;
        Ok(self.act(r, x))
    }
    pub fn homomorphism(&self, target: &Self, mapping: &[usize]) -> MathResult<ModuleHomomorphism> {
        require(
            self.ring == target.ring,
            "module maps require the same base ring tables",
        )?;
        let additive = self.additive.homomorphism(&target.additive, mapping)?;
        for r in 0..self.ring.order() {
            for x in 0..self.additive.n {
                require(
                    mapping[self.act(r, x)] == target.act(r, mapping[x]),
                    "module map scalar law fails",
                )?;
            }
        }
        Ok(ModuleHomomorphism { additive })
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModuleHomomorphism {
    additive: GroupHomomorphism,
}
impl ModuleHomomorphism {
    pub fn mapping(&self) -> &[usize] {
        self.additive.mapping()
    }
    pub fn kernel(&self) -> &Subgroup {
        self.additive.kernel()
    }
    pub fn image(&self) -> &Subgroup {
        self.additive.image()
    }
    pub fn is_injective(&self) -> bool {
        self.additive.is_injective()
    }
    pub fn is_surjective(&self) -> bool {
        self.additive.is_surjective()
    }
}
