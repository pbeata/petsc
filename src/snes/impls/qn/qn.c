#include <private/snesimpl.h>

typedef enum {SNES_QN_SEQUENTIAL, SNES_QN_COMPOSED} SNESQNCompositionType;

typedef struct {
  Vec         *dX;              /* The change in X */
  Vec         *dF;              /* The change in F */
  PetscInt    m;                /* the number of kept previous steps */
  PetscScalar *alpha;
  PetscScalar *beta;
  PetscScalar *rho;
  PetscViewer monitor;
  PetscReal   powell_gamma;     /* Powell angle restart condition */
  PetscReal   powell_downhill;  /* Powell descent restart condition */
  PetscReal   scaling;          /* scaling of H0 */

  SNESQNCompositionType compositiontype; /* determine if the composition is done sequentially or as a composition */

} SNES_QN;

#undef __FUNCT__
#define __FUNCT__ "LBGFSApplyJinv_Private"
PetscErrorCode LBGFSApplyJinv_Private(SNES snes, PetscInt it, Vec D, Vec Y) {

  PetscErrorCode ierr;

  SNES_QN *qn = (SNES_QN*)snes->data;

  Vec *dX = qn->dX;
  Vec *dF = qn->dF;

  PetscScalar *alpha = qn->alpha;
  PetscScalar *beta = qn->beta;
  PetscScalar *rho = qn->rho;

  PetscInt k, i;
  PetscInt m = qn->m;
  PetscScalar t;
  PetscInt l = m;

  PetscFunctionBegin;

  ierr = VecCopy(D, Y);CHKERRQ(ierr);

  if (it < m) l = it;

  /* outward recursion starting at iteration k's update and working back */
  for (i = 0; i < l; i++) {
    k = (it - i - 1) % l;
    ierr = VecDot(dX[k], Y, &t);CHKERRQ(ierr);
    alpha[k] = t*rho[k];
    if (qn->monitor) {
      ierr = PetscViewerASCIIAddTab(qn->monitor,((PetscObject)snes)->tablevel+2);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(qn->monitor, "it: %d k: %d alpha:        %14.12e\n", it, k, PetscRealPart(alpha[k]));CHKERRQ(ierr);
      ierr = PetscViewerASCIISubtractTab(qn->monitor,((PetscObject)snes)->tablevel+2);CHKERRQ(ierr);
    }
    ierr = VecAXPY(Y, -alpha[k], dF[k]);CHKERRQ(ierr);
  }

  ierr = VecScale(Y, qn->scaling);CHKERRQ(ierr);

  /* inward recursion starting at the first update and working forward */
  for (i = 0; i < l; i++) {
    k = (it + i - l) % l;
    ierr = VecDot(dF[k], Y, &t);CHKERRQ(ierr);
    beta[k] = rho[k]*t;
    ierr = VecAXPY(Y, (alpha[k] - beta[k]), dX[k]);
    if (qn->monitor) {
      ierr = PetscViewerASCIIAddTab(qn->monitor,((PetscObject)snes)->tablevel+2);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(qn->monitor, "it: %d k: %d alpha - beta: %14.12e\n", it, k, PetscRealPart(alpha[k] - beta[k]));CHKERRQ(ierr);
      ierr = PetscViewerASCIISubtractTab(qn->monitor,((PetscObject)snes)->tablevel+2);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SNESSolve_QN"
static PetscErrorCode SNESSolve_QN(SNES snes)
{

  PetscErrorCode ierr;
  SNES_QN *qn = (SNES_QN*) snes->data;

  Vec X, Xold;
  Vec F, G, B;
  Vec W, Y, FPC, D, Dold;
  SNESConvergedReason reason;
  PetscInt i, i_r, k;

  PetscReal fnorm, xnorm = 0, ynorm, gnorm;
  PetscInt m = qn->m;
  PetscBool lssucceed, changed;

  PetscScalar rhosc;

  Vec *dX = qn->dX;
  Vec *dF = qn->dF;
  PetscScalar *rho = qn->rho;
  PetscScalar DolddotD, DolddotDold, DdotD, YdotD, a;

  /* basically just a regular newton's method except for the application of the jacobian */
  PetscFunctionBegin;

  X             = snes->vec_sol;        /* solution vector */
  F             = snes->vec_func;       /* residual vector */
  Y             = snes->vec_sol_update; /* search direction generated by J^-1D*/
  B             = snes->vec_rhs;
  G             = snes->work[0];
  W             = snes->work[1];
  Xold          = snes->work[2];

  /* directions generated by the preconditioned problem with F_pre = F or x - M(x, b) */
  D             = snes->work[3];
  Dold          = snes->work[4];

  snes->reason = SNES_CONVERGED_ITERATING;

  ierr = PetscObjectTakeAccess(snes);CHKERRQ(ierr);
  snes->iter = 0;
  snes->norm = 0.;
  ierr = PetscObjectGrantAccess(snes);CHKERRQ(ierr);
  ierr = SNESComputeFunction(snes,X,F);CHKERRQ(ierr);
  if (snes->domainerror) {
    snes->reason = SNES_DIVERGED_FUNCTION_DOMAIN;
    PetscFunctionReturn(0);
  }
  ierr = VecNorm(F, NORM_2, &fnorm);CHKERRQ(ierr); /* fnorm <- ||F||  */
  if (PetscIsInfOrNanReal(fnorm)) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_FP,"Infinite or not-a-number generated in norm");
  ierr = PetscObjectTakeAccess(snes);CHKERRQ(ierr);
  snes->norm = fnorm;
  ierr = PetscObjectGrantAccess(snes);CHKERRQ(ierr);
  SNESLogConvHistory(snes,fnorm,0);
  ierr = SNESMonitor(snes,0,fnorm);CHKERRQ(ierr);

  /* set parameter for default relative tolerance convergence test */
   snes->ttol = fnorm*snes->rtol;
  /* test convergence */
  ierr = (*snes->ops->converged)(snes,0,0.0,0.0,fnorm,&snes->reason,snes->cnvP);CHKERRQ(ierr);
  if (snes->reason) PetscFunctionReturn(0);

  /* composed solve -- either sequential or composed */
  if (snes->pc) {
    if (qn->compositiontype == SNES_QN_SEQUENTIAL) {
      ierr = SNESSolve(snes->pc, B, X);CHKERRQ(ierr);
      ierr = SNESGetConvergedReason(snes->pc,&reason);CHKERRQ(ierr);
      if (reason < 0 && (reason != SNES_DIVERGED_MAX_IT)) {
        snes->reason = SNES_DIVERGED_INNER;
        PetscFunctionReturn(0);
      }
      ierr = SNESGetFunction(snes->pc, &FPC, PETSC_NULL, PETSC_NULL);CHKERRQ(ierr);
      ierr = VecCopy(FPC, F);CHKERRQ(ierr);
      ierr = SNESGetFunctionNorm(snes->pc, &fnorm);CHKERRQ(ierr);
      ierr = VecCopy(F, Y);CHKERRQ(ierr);
    } else {
      ierr = VecCopy(X, Y);CHKERRQ(ierr);
      ierr = SNESSolve(snes->pc, B, Y);CHKERRQ(ierr);
      ierr = SNESGetConvergedReason(snes->pc,&reason);CHKERRQ(ierr);
      if (reason < 0 && (reason != SNES_DIVERGED_MAX_IT)) {
        snes->reason = SNES_DIVERGED_INNER;
        PetscFunctionReturn(0);
      }
      ierr = VecAYPX(Y,-1.0,X);CHKERRQ(ierr);
    }
  } else {
    ierr = VecCopy(F, Y);CHKERRQ(ierr);
  }
  ierr = VecCopy(Y, D);CHKERRQ(ierr);

  for(i = 0, i_r = 0; i < snes->max_its; i++, i_r++) {
    /* line search for lambda */
    ynorm = 1; gnorm = fnorm;
    ierr = VecCopy(D, Dold);CHKERRQ(ierr);
    ierr = VecCopy(X, Xold);CHKERRQ(ierr);
    ierr = SNESLineSearchPreCheckApply(snes,X,Y,&changed);CHKERRQ(ierr);
    ierr = SNESLineSearchApply(snes,X,F,Y,fnorm,xnorm,W,G,&ynorm,&gnorm,&lssucceed);CHKERRQ(ierr);
    if (snes->reason == SNES_DIVERGED_FUNCTION_COUNT) break;
    if (snes->domainerror) {
      snes->reason = SNES_DIVERGED_FUNCTION_DOMAIN;
      PetscFunctionReturn(0);
      }
    if (!lssucceed) {
      if (++snes->numFailures >= snes->maxFailures) {
        snes->reason = SNES_DIVERGED_LINE_SEARCH;
        break;
      }
    }

    /* Update function and solution vectors */
    fnorm = gnorm;
    ierr = VecCopy(G,F);CHKERRQ(ierr);
    ierr = VecCopy(W,X);CHKERRQ(ierr);

    /* convergence monitoring */
    ierr = PetscInfo4(snes,"fnorm=%18.16e, gnorm=%18.16e, ynorm=%18.16e, lssucceed=%d\n",(double)fnorm,(double)gnorm,(double)ynorm,(int)lssucceed);CHKERRQ(ierr);

    ierr = SNESSetIterationNumber(snes, i+1);CHKERRQ(ierr);
    ierr = SNESSetFunctionNorm(snes, fnorm);CHKERRQ(ierr);

    SNESLogConvHistory(snes,snes->norm,snes->iter);
    ierr = SNESMonitor(snes,snes->iter,snes->norm);CHKERRQ(ierr);
    /* set parameter for default relative tolerance convergence test */
    ierr = (*snes->ops->converged)(snes,snes->iter,0.0,0.0,fnorm,&snes->reason,snes->cnvP);CHKERRQ(ierr);
    if (snes->reason) PetscFunctionReturn(0);


    if (snes->pc) {
      if (qn->compositiontype == SNES_QN_SEQUENTIAL) {
        ierr = SNESSolve(snes->pc, B, X);CHKERRQ(ierr);
        ierr = SNESGetConvergedReason(snes->pc,&reason);CHKERRQ(ierr);
        if (reason < 0 && (reason != SNES_DIVERGED_MAX_IT)) {
          snes->reason = SNES_DIVERGED_INNER;
          PetscFunctionReturn(0);
        }
        ierr = SNESGetFunction(snes->pc, &FPC, PETSC_NULL, PETSC_NULL);CHKERRQ(ierr);
        ierr = VecCopy(FPC, F);CHKERRQ(ierr);
        ierr = SNESGetFunctionNorm(snes->pc, &fnorm);CHKERRQ(ierr);
        ierr = VecCopy(F, D);CHKERRQ(ierr);
      } else {
        ierr = VecCopy(X, D);CHKERRQ(ierr);
        ierr = SNESSolve(snes->pc, B, D);CHKERRQ(ierr);
        ierr = SNESGetConvergedReason(snes->pc,&reason);CHKERRQ(ierr);
        if (reason < 0 && (reason != SNES_DIVERGED_MAX_IT)) {
          snes->reason = SNES_DIVERGED_INNER;
          PetscFunctionReturn(0);
        }
        ierr = VecAYPX(D,-1.0,X);CHKERRQ(ierr);
      }
    } else {
      ierr = VecCopy(F, D);CHKERRQ(ierr);
    }

    /* check restart by Powell's Criterion: |F^T H_0 Fold| > 0.2 * |Fold^T H_0 Fold| */
    ierr = VecDot(Dold, Dold, &DolddotDold);CHKERRQ(ierr);
    ierr = VecDot(Dold, D, &DolddotD);CHKERRQ(ierr);
    ierr = VecDot(D, D, &DdotD);CHKERRQ(ierr);
    ierr = VecDot(Y, D, &YdotD);CHKERRQ(ierr);
    if (PetscAbs(PetscRealPart(DolddotD)) > qn->powell_gamma*PetscAbs(PetscRealPart(DolddotDold))) {
      if (qn->monitor) {
        ierr = PetscViewerASCIIAddTab(qn->monitor,((PetscObject)snes)->tablevel+2);CHKERRQ(ierr);
        ierr = PetscViewerASCIIPrintf(qn->monitor, "restart! |%14.12e| > %4.2f*|%14.12e|\n", k, PetscRealPart(DolddotD), qn->powell_gamma, PetscRealPart(DolddotDold));CHKERRQ(ierr);
        ierr = PetscViewerASCIISubtractTab(qn->monitor,((PetscObject)snes)->tablevel+2);CHKERRQ(ierr);
      }
      i_r = -1;
      /* general purpose update */
      if (snes->ops->update) {
        ierr = (*snes->ops->update)(snes, snes->iter);CHKERRQ(ierr);
      }
      ierr = VecCopy(D, Y);CHKERRQ(ierr);
    } else {
      /* set the differences */
      k = i_r % m;
      ierr = VecCopy(D, dF[k]);CHKERRQ(ierr);
      ierr = VecAXPY(dF[k], -1.0, Dold);CHKERRQ(ierr);
      ierr = VecCopy(X, dX[k]);CHKERRQ(ierr);
      ierr = VecAXPY(dX[k], -1.0, Xold);CHKERRQ(ierr);
      ierr = VecDot(dX[k], dF[k], &rhosc);CHKERRQ(ierr);

      /* set scaling to be shanno scaling */
      rho[k] = 1. / rhosc;
      ierr = VecDot(dF[k], dF[k], &a);CHKERRQ(ierr);
      qn->scaling = PetscRealPart(rhosc) / PetscRealPart(a);

      /* general purpose update */
      if (snes->ops->update) {
        ierr = (*snes->ops->update)(snes, snes->iter);CHKERRQ(ierr);
      }
      /* apply the current iteration of the approximate jacobian in order to get the next search direction*/
      ierr = LBGFSApplyJinv_Private(snes, i_r+1, D, Y);CHKERRQ(ierr);
    }
}
  if (i == snes->max_its) {
    ierr = PetscInfo1(snes, "Maximum number of iterations has been reached: %D\n", snes->max_its);CHKERRQ(ierr);
    if (!snes->reason) snes->reason = SNES_DIVERGED_MAX_IT;
  }
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "SNESSetUp_QN"
static PetscErrorCode SNESSetUp_QN(SNES snes)
{
  SNES_QN *qn = (SNES_QN*)snes->data;
  PetscErrorCode ierr;
  PetscFunctionBegin;
  ierr = VecDuplicateVecs(snes->vec_sol, qn->m, &qn->dX);CHKERRQ(ierr);
  ierr = VecDuplicateVecs(snes->vec_sol, qn->m, &qn->dF);CHKERRQ(ierr);
  ierr = PetscMalloc3(qn->m, PetscScalar, &qn->alpha, qn->m, PetscScalar, &qn->beta, qn->m, PetscScalar, &qn->rho);CHKERRQ(ierr);
  ierr = SNESDefaultGetWork(snes,5);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SNESReset_QN"
static PetscErrorCode SNESReset_QN(SNES snes)
{
  PetscErrorCode ierr;
  SNES_QN *qn;
  PetscFunctionBegin;
  if (snes->data) {
    qn = (SNES_QN*)snes->data;
    if (qn->dX) {
      ierr = VecDestroyVecs(qn->m, &qn->dX);CHKERRQ(ierr);
    }
    if (qn->dF) {
      ierr = VecDestroyVecs(qn->m, &qn->dF);CHKERRQ(ierr);
    }
    ierr = PetscFree3(qn->alpha, qn->beta, qn->rho);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SNESDestroy_QN"
static PetscErrorCode SNESDestroy_QN(SNES snes)
{
  PetscErrorCode ierr;
  PetscFunctionBegin;
  ierr = SNESReset_QN(snes);CHKERRQ(ierr);
  ierr = PetscFree(snes->data);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunctionDynamic((PetscObject)snes,"","",PETSC_NULL);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SNESSetFromOptions_QN"
static PetscErrorCode SNESSetFromOptions_QN(SNES snes)
{

  PetscErrorCode ierr;
  SNES_QN    *qn;
  const char *compositions[] = {"composed", "sequential"};
  PetscInt   indx = 0;
  PetscBool  flg;
  PetscBool  monflg = PETSC_FALSE;
  PetscFunctionBegin;

  qn = (SNES_QN*)snes->data;

  ierr = PetscOptionsHead("SNES QN options");CHKERRQ(ierr);
  ierr = PetscOptionsInt("-snes_qn_m",                "Number of past states saved for L-BFGS methods", "SNES", qn->m, &qn->m, PETSC_NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-snes_qn_powell_gamma",    "Powell angle tolerance",          "SNES", qn->powell_gamma, &qn->powell_gamma, PETSC_NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-snes_qn_powell_downhill", "Powell descent tolerance",        "SNES", qn->powell_downhill, &qn->powell_downhill, PETSC_NULL);CHKERRQ(ierr);
  ierr = PetscOptionsBool("-snes_qn_monitor",         "Monitor for the QN methods",      "SNES", monflg, &monflg, PETSC_NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEList("-snes_qn_composition",    "Composition type",                "SNES",compositions,2,"sequential",(PetscInt *)&qn->compositiontype,&flg);CHKERRQ(ierr);
  if (flg) {
    switch (indx) {
    case 0: qn->compositiontype = SNES_QN_COMPOSED;
    case 1: qn->compositiontype = SNES_QN_SEQUENTIAL;
    }
  }
    ierr = PetscOptionsTail();CHKERRQ(ierr);
  if (monflg) {
    qn->monitor = PETSC_VIEWER_STDOUT_(((PetscObject)snes)->comm);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

EXTERN_C_BEGIN
#undef __FUNCT__
#define __FUNCT__ "SNESLineSearchSetType_QN"
PetscErrorCode  SNESLineSearchSetType_QN(SNES snes, SNESLineSearchType type)
{
  PetscErrorCode ierr;
  PetscFunctionBegin;

  switch (type) {
  case SNES_LS_BASIC:
    ierr = SNESLineSearchSet(snes,SNESLineSearchNo,PETSC_NULL);CHKERRQ(ierr);
    break;
  case SNES_LS_BASIC_NONORMS:
    ierr = SNESLineSearchSet(snes,SNESLineSearchNoNorms,PETSC_NULL);CHKERRQ(ierr);
    break;
  case SNES_LS_QUADRATIC:
    ierr = SNESLineSearchSet(snes,SNESLineSearchQuadraticSecant,PETSC_NULL);CHKERRQ(ierr);
    break;
  case SNES_LS_SECANT:
    ierr = SNESLineSearchSet(snes,SNESLineSearchSecant,PETSC_NULL);CHKERRQ(ierr);
    break;
  default:
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_SUP,"Unknown line search type");
    break;
  }
  snes->ls_type = type;
  PetscFunctionReturn(0);
}
EXTERN_C_END


/* -------------------------------------------------------------------------- */
/*MC
      SNESQN - Limited-Memory Quasi-Newton methods for the solution of nonlinear systems.

      Options Database:

+     -snes_qn_m - Number of past states saved for the L-Broyden methods.
-     -snes_qn_monitor - Monitors the quasi-newton jacobian.

      Notes: This implements the L-BFGS algorithm for the solution of F(x) = b using previous change in F(x) and x to
      form the approximate inverse Jacobian using a series of multiplicative rank-one updates.  This will eventually be
      generalized to implement several limited-memory Broyden methods.

      References:

      L-Broyden Methods: a generalization of the L-BFGS method to the limited memory Broyden family, M. B. Reed,
      International Journal of Computer Mathematics, vol. 86, 2009.


      Level: beginner

.seealso:  SNESCreate(), SNES, SNESSetType(), SNESLS, SNESTR

M*/
EXTERN_C_BEGIN
#undef __FUNCT__
#define __FUNCT__ "SNESCreate_QN"
PetscErrorCode  SNESCreate_QN(SNES snes)
{

  PetscErrorCode ierr;
  SNES_QN *qn;

  PetscFunctionBegin;
  snes->ops->setup           = SNESSetUp_QN;
  snes->ops->solve           = SNESSolve_QN;
  snes->ops->destroy         = SNESDestroy_QN;
  snes->ops->setfromoptions  = SNESSetFromOptions_QN;
  snes->ops->view            = 0;
  snes->ops->reset           = SNESReset_QN;

  snes->usespc          = PETSC_TRUE;
  snes->usesksp         = PETSC_FALSE;

  snes->max_funcs = 30000;
  snes->max_its   = 10000;

  ierr = PetscNewLog(snes,SNES_QN,&qn);CHKERRQ(ierr);
  snes->data = (void *) qn;
  qn->m               = 30;
  qn->scaling         = 1.0;
  qn->dX              = PETSC_NULL;
  qn->dF              = PETSC_NULL;
  qn->monitor         = PETSC_NULL;
  qn->powell_gamma    = 0.9;
  qn->powell_downhill = 0.2;
  qn->compositiontype = SNES_QN_SEQUENTIAL;

  ierr = PetscObjectComposeFunctionDynamic((PetscObject)snes,"SNESLineSearchSetType_C","SNESLineSearchSetType_QN",SNESLineSearchSetType_QN);CHKERRQ(ierr);
  ierr = SNESLineSearchSetType(snes, SNES_LS_SECANT);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}
EXTERN_C_END
