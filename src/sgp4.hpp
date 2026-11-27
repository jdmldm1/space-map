// SGP4 near-Earth propagator (period < 225 min), after Hoots & Roehrich / Vallado (WGS-72 constants).
// Deep-space objects (period >= 225 min) are not handled here; callers fall back to Keplerian + J2.
#pragma once
#include <cmath>

struct Sgp4 {

    static constexpr double RE = 6378.135;
    static constexpr double XKE = 0.0743669161331734132;
    static constexpr double J2 = 0.001082616, J3 = -0.00000253881, J4 = -0.00000165597;
    static constexpr double TWOPI = 6.28318530717958647692;

    bool ok = false, isimp = false;
    double bstar = 0, ecco = 0, argpo = 0, inclo = 0, mo = 0, no = 0, nodeo = 0;
    double aycof = 0, cc1 = 0, cc4 = 0, cc5 = 0, con41 = 0, d2 = 0, d3 = 0, d4 = 0, delmo = 0, eta = 0;
    double argpdot = 0, omgcof = 0, sinmao = 0, t2cof = 0, t3cof = 0, t4cof = 0, t5cof = 0;
    double x1mth2 = 0, x7thm1 = 0, mdot = 0, nodedot = 0, xlcof = 0, xmcof = 0, nodecf = 0;

    bool init(double no_kozai, double ecc, double incl, double raan, double argp, double M, double bstar_) {
        ok = false;
        if (no_kozai <= 0 || ecc < 0 || ecc >= 1.0) return false;
        if (TWOPI / no_kozai >= 225.0) return false;
        bstar = bstar_; ecco = ecc; argpo = argp; inclo = incl; mo = M; nodeo = raan;
        const double j3oj2 = J3 / J2, x2o3 = 2.0 / 3.0;

        const double eccsq = ecco * ecco, omeosq = 1.0 - eccsq, rteosq = std::sqrt(omeosq);
        const double cosio = std::cos(inclo), cosio2 = cosio * cosio;
        double n = no_kozai;
        const double ak = std::pow(XKE / n, x2o3);
        const double d1 = 0.75 * J2 * (3.0 * cosio2 - 1.0) / (rteosq * omeosq);
        double del = d1 / (ak * ak);
        const double adel = ak * (1.0 - del * del - del * (1.0 / 3.0 + 134.0 * del * del / 81.0));
        del = d1 / (adel * adel);
        n = n / (1.0 + del);
        no = n;
        const double ao = std::pow(XKE / n, x2o3);
        const double sinio = std::sin(inclo);
        const double po = ao * omeosq;
        const double con42 = 1.0 - 5.0 * cosio2;
        con41 = -con42 - cosio2 - cosio2;
        const double posq = po * po, rp = ao * (1.0 - ecco);

        isimp = rp < (220.0 / RE + 1.0);
        const double ss = 78.0 / RE + 1.0;
        const double qzms2t = std::pow((120.0 - 78.0) / RE, 4.0);
        double sfour = ss, qzms24 = qzms2t;
        const double perige = (rp - 1.0) * RE;
        if (perige < 156.0) {
            sfour = perige - 78.0;
            if (perige < 98.0) sfour = 20.0;
            qzms24 = std::pow((120.0 - sfour) / RE, 4.0);
            sfour = sfour / RE + 1.0;
        }
        const double pinvsq = 1.0 / posq;
        const double tsi = 1.0 / (ao - sfour);
        eta = ao * ecco * tsi;
        const double etasq = eta * eta, eeta = ecco * eta, psisq = std::fabs(1.0 - etasq);
        const double coef = qzms24 * std::pow(tsi, 4.0);
        const double coef1 = coef / std::pow(psisq, 3.5);
        const double cc2 = coef1 * n * (ao * (1.0 + 1.5 * etasq + eeta * (4.0 + etasq)) +
                                        0.375 * J2 * tsi / psisq * con41 * (8.0 + 3.0 * etasq * (8.0 + etasq)));
        cc1 = bstar * cc2;
        double cc3 = 0.0;
        if (ecco > 1.0e-4) cc3 = -2.0 * coef * tsi * j3oj2 * n * sinio / ecco;
        x1mth2 = 1.0 - cosio2;
        cc4 = 2.0 * n * coef1 * ao * omeosq *
              (eta * (2.0 + 0.5 * etasq) + ecco * (0.5 + 2.0 * etasq) -
               J2 * tsi / (ao * psisq) *
                   (-3.0 * con41 * (1.0 - 2.0 * eeta + etasq * (1.5 - 0.5 * eeta)) +
                    0.75 * x1mth2 * (2.0 * etasq - eeta * (1.0 + etasq)) * std::cos(2.0 * argpo)));
        cc5 = 2.0 * coef1 * ao * omeosq * (1.0 + 2.75 * (etasq + eeta) + eeta * etasq);
        const double cosio4 = cosio2 * cosio2;
        const double temp1 = 1.5 * J2 * pinvsq * n;
        const double temp2 = 0.5 * temp1 * J2 * pinvsq;
        const double temp3 = -0.46875 * J4 * pinvsq * pinvsq * n;
        mdot = n + 0.5 * temp1 * rteosq * con41 + 0.0625 * temp2 * rteosq * (13.0 - 78.0 * cosio2 + 137.0 * cosio4);
        argpdot = -0.5 * temp1 * con42 + 0.0625 * temp2 * (7.0 - 114.0 * cosio2 + 395.0 * cosio4) +
                  temp3 * (3.0 - 36.0 * cosio2 + 49.0 * cosio4);
        const double xhdot1 = -temp1 * cosio;
        nodedot = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * cosio2) + 2.0 * temp3 * (3.0 - 7.0 * cosio2)) * cosio;
        omgcof = bstar * cc3 * std::cos(argpo);
        xmcof = 0.0;
        if (ecco > 1.0e-4) xmcof = -x2o3 * coef * bstar / eeta;
        nodecf = 3.5 * omeosq * xhdot1 * cc1;
        t2cof = 1.5 * cc1;
        const double denom = (std::fabs(cosio + 1.0) > 1.5e-12) ? (1.0 + cosio) : 1.5e-12;
        xlcof = -0.25 * j3oj2 * sinio * (3.0 + 5.0 * cosio) / denom;
        aycof = -0.5 * j3oj2 * sinio;
        const double delmotemp = 1.0 + eta * std::cos(mo);
        delmo = delmotemp * delmotemp * delmotemp;
        sinmao = std::sin(mo);
        x7thm1 = 7.0 * cosio2 - 1.0;
        if (!isimp) {
            const double cc1sq = cc1 * cc1;
            d2 = 4.0 * ao * tsi * cc1sq;
            const double temp = d2 * tsi * cc1 / 3.0;
            d3 = (17.0 * ao + sfour) * temp;
            d4 = 0.5 * temp * ao * tsi * (221.0 * ao + 31.0 * sfour) * cc1;
            t3cof = d2 + 2.0 * cc1sq;
            t4cof = 0.25 * (3.0 * d3 + cc1 * (12.0 * d2 + 10.0 * cc1sq));
            t5cof = 0.2 * (3.0 * d4 + 12.0 * cc1 * d3 + 6.0 * d2 * d2 + 15.0 * cc1sq * (2.0 * d2 + cc1sq));
        }
        ok = true;
        return true;
    }

    bool propagate(double tsince, double r[3], double v[3] = nullptr) const {
        if (!ok) return false;
        const double x2o3 = 2.0 / 3.0;
        const double t = tsince;
        const double xmdf = mo + mdot * t, argpdf = argpo + argpdot * t, nodedf = nodeo + nodedot * t;
        double argpm = argpdf, mm = xmdf;
        const double t2 = t * t;
        double nodem = nodedf + nodecf * t2;
        double tempa = 1.0 - cc1 * t, tempe = bstar * cc4 * t, templ = t2cof * t2;
        if (!isimp) {
            const double delomg = omgcof * t;
            const double delmtemp = 1.0 + eta * std::cos(xmdf);
            const double delm = xmcof * (delmtemp * delmtemp * delmtemp - delmo);
            const double temp = delomg + delm;
            mm = xmdf + temp;
            argpm = argpdf - temp;
            const double t3 = t2 * t, t4 = t3 * t;
            tempa = tempa - d2 * t2 - d3 * t3 - d4 * t4;
            tempe = tempe + bstar * cc5 * (std::sin(mm) - sinmao);
            templ = templ + t3cof * t3 + t4 * (t4cof + t * t5cof);
        }
        double nm = no, em = ecco;
        const double inclm = inclo;
        if (nm <= 0.0) return false;
        const double am = std::pow(XKE / nm, x2o3) * tempa * tempa;
        nm = XKE / std::pow(am, 1.5);
        em = em - tempe;
        if (em >= 1.0 || em < -0.001 || am < 0.95) return false;
        if (em < 1.0e-6) em = 1.0e-6;
        mm = mm + no * templ;
        double xlm = mm + argpm + nodem;
        nodem = std::fmod(nodem, TWOPI);
        argpm = std::fmod(argpm, TWOPI);
        xlm = std::fmod(xlm, TWOPI);
        mm = std::fmod(xlm - argpm - nodem, TWOPI);

        const double sinip = std::sin(inclm), cosip = std::cos(inclm);
        const double ep = em, xincp = inclm, argpp = argpm, nodep = nodem, mp = mm;
        const double axnl = ep * std::cos(argpp);
        double temp = 1.0 / (am * (1.0 - ep * ep));
        const double aynl = ep * std::sin(argpp) + temp * aycof;
        const double xl = mp + argpp + nodep + temp * xlcof * axnl;

        const double u = std::fmod(xl - nodep, TWOPI);
        double eo1 = u, tem5 = 9999.9, sineo1 = 0, coseo1 = 0;
        for (int ktr = 1; std::fabs(tem5) >= 1.0e-12 && ktr <= 10; ++ktr) {
            sineo1 = std::sin(eo1); coseo1 = std::cos(eo1);
            tem5 = 1.0 - coseo1 * axnl - sineo1 * aynl;
            tem5 = (u - aynl * coseo1 + axnl * sineo1 - eo1) / tem5;
            if (std::fabs(tem5) >= 0.95) tem5 = tem5 > 0.0 ? 0.95 : -0.95;
            eo1 += tem5;
        }
        const double ecose = axnl * coseo1 + aynl * sineo1, esine = axnl * sineo1 - aynl * coseo1;
        const double el2 = axnl * axnl + aynl * aynl, pl = am * (1.0 - el2);
        if (pl < 0.0) return false;
        const double rl = am * (1.0 - ecose), rdotl = std::sqrt(am) * esine / rl, rvdotl = std::sqrt(pl) / rl;
        const double betal = std::sqrt(1.0 - el2);
        temp = esine / (1.0 + betal);
        const double sinu = am / rl * (sineo1 - aynl - axnl * temp), cosu = am / rl * (coseo1 - axnl + aynl * temp);
        double su = std::atan2(sinu, cosu);
        const double sin2u = (cosu + cosu) * sinu, cos2u = 1.0 - 2.0 * sinu * sinu;
        temp = 1.0 / pl;
        const double temp1 = 0.5 * J2 * temp, temp2 = temp1 * temp;
        const double mrt = rl * (1.0 - 1.5 * temp2 * betal * con41) + 0.5 * temp1 * x1mth2 * cos2u;
        su = su - 0.25 * temp2 * x7thm1 * sin2u;
        const double xnode = nodep + 1.5 * temp2 * cosip * sin2u;
        const double xinc = xincp + 1.5 * temp2 * cosip * sinip * cos2u;
        const double mvt = rdotl - nm * temp1 * x1mth2 * sin2u / XKE;
        const double rvdot = rvdotl + nm * temp1 * (x1mth2 * cos2u + 1.5 * con41) / XKE;
        const double sinsu = std::sin(su), cossu = std::cos(su), snod = std::sin(xnode), cnod = std::cos(xnode);
        const double sini = std::sin(xinc), cosi = std::cos(xinc);
        const double xmx = -snod * cosi, xmy = cnod * cosi;
        const double ux = xmx * sinsu + cnod * cossu, uy = xmy * sinsu + snod * cossu, uz = sini * sinsu;
        const double vx = xmx * cossu - cnod * sinsu, vy = xmy * cossu - snod * sinsu, vz = sini * cossu;
        if (mrt < 1.0) return false;
        r[0] = mrt * ux * RE; r[1] = mrt * uy * RE; r[2] = mrt * uz * RE;
        if (v) {
            const double vk = RE * XKE / 60.0;
            v[0] = (mvt * ux + rvdot * vx) * vk; v[1] = (mvt * uy + rvdot * vy) * vk; v[2] = (mvt * uz + rvdot * vz) * vk;
        }
        return true;
    }
};
