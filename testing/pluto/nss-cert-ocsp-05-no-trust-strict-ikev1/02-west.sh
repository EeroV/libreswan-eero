/testing/guestbin/swan-prep --x509
ipsec certutil -D -n east
ipsec start
../../guestbin/wait-until-pluto-started
ipsec whack --impair delete-on-retransmit
ipsec auto --add nss-cert-ocsp
ipsec auto --status |grep nss-cert-ocsp
echo "initdone"
ipsec auto --up nss-cert-ocsp
echo done
