#include "ping.h"            

struct proto	proto_v4 = { proc_v4, send_v4, NULL, NULL, 0, IPPROTO_ICMP };

#ifdef	IPV6
struct proto	proto_v6 = { proc_v6, send_v6, NULL, NULL, 0, IPPROTO_ICMPV6 };
#endif

int	datalen = 56;		/* data that goes with ICMP echo request */
int broadcast_flag = 0;
int ttl;
int ttl_flag = 0;
int quiet_flag = 0;
int count = 64;
int send_interval = 1;
double rttMax = -1;
double rttMin = 1e8;
double rttTotal = 0;
int recvCount = 0;
int sendCount = 0;
int initial_num = 0;
int data_flag = 0;
int detail_flag = 0;
struct timeval tvalBegin;

int main(int argc, char **argv)
{
	int				c;
	struct addrinfo	*ai;

	opterr = 0;		/* don't want getopt() writing to stderr */
	while ( (c = getopt(argc, argv, "hbt:qc:s:i:n:dv")) != -1) {
		switch (c) {
		case 'h':
			help();
			break;

		case 'b':
			broadcast_flag = 1;
			break;
		
		case 't':
			sscanf(optarg, "%d", &ttl);
			if(ttl >= 0 && ttl < 256)
				ttl_flag = 1;
			break;

		case 'q':
			quiet_flag = 1;
			break;

		case 'c':
			sscanf(optarg, "%d", &count);
			break;

		case 's':
			sscanf(optarg, "%d", &datalen);
			break;

		case 'i':
			sscanf(optarg, "%d", &send_interval);
			break;

		case 'n':
			sscanf(optarg, "%d", &initial_num);
			break;

		case 'd':
			data_flag = 1;
			break;

		case 'v':
			detail_flag = 1;
			break;
		
		case '?':
			err_quit("unrecognized option: %c", c);
		}
	}

	if (optind != argc-1)
		err_quit("usage: ping [ -v ] <hostname>");
	host = argv[optind];

	pid = getpid();
	signal(SIGALRM, sig_alrm);

	ai = host_serv(host, NULL, 0, 0);

	printf("ping %s (%s): %d data bytes\n", ai->ai_canonname,
		   Sock_ntop_host(ai->ai_addr, ai->ai_addrlen), datalen);

		/* 4initialize according to protocol */
	if (ai->ai_family == AF_INET) {
		pr = &proto_v4;
#ifdef	IPV6
	} else if (ai->ai_family == AF_INET6) {
		pr = &proto_v6;
		if (IN6_IS_ADDR_V4MAPPED(&(((struct sockaddr_in6 *)
								 ai->ai_addr)->sin6_addr)))
			err_quit("cannot ping IPv4-mapped IPv6 address");
#endif
	} else
		err_quit("unknown address family %d", ai->ai_family);

	pr->sasend = ai->ai_addr;
	pr->sarecv = calloc(1, ai->ai_addrlen);
	pr->salen = ai->ai_addrlen;

	readloop();

	exit(0);
}

void
proc_v4(char *ptr, ssize_t len, struct timeval *tvrecv)
{
	int				hlen1, icmplen;
	double			rtt;
	struct ip		*ip;
	struct icmp		*icmp;
	struct timeval	*tvsend;

	ip = (struct ip *) ptr;		/* start of IP header */
	hlen1 = ip->ip_hl << 2;		/* length of IP header */

	icmp = (struct icmp *) (ptr + hlen1);	/* start of ICMP header */
	if ( (icmplen = len - hlen1) < 8)
		err_quit("icmplen (%d) < 8", icmplen);

	if (icmp->icmp_type == ICMP_ECHOREPLY) {
		if (icmp->icmp_id != pid)
			return;			/* not a response to our ECHO_REQUEST */
		if (icmplen < 16)
			err_quit("icmplen (%d) < 16", icmplen);

		tvsend = (struct timeval *) icmp->icmp_data;
		tv_sub(tvrecv, tvsend);
		rtt = tvrecv->tv_sec * 1000.0 + tvrecv->tv_usec / 1000.0;

		if (rtt > rttMax)
			rttMax = rtt;
		if (rtt < rttMin)
			rttMin = rtt; 
		rttTotal += rtt;
		recvCount++;
		if(!quiet_flag){
			if(data_flag){
				double date = time(NULL) + tvrecv->tv_usec / 1000000.0; 
				printf("[%.6lf]", date);
			}
			if(!detail_flag)
				printf("%d bytes from %s: seq=%u, ttl=%d, rtt=%.3f ms\n",
					icmplen, Sock_ntop_host(pr->sarecv, pr->salen),
					icmp->icmp_seq + initial_num, ip->ip_ttl, rtt);
			else
				printf("  %d bytes from %s: type = %d, code = %d\n",
				icmplen, Sock_ntop_host(pr->sarecv, pr->salen),
				icmp->icmp_type, icmp->icmp_code);
		}
		if (icmp->icmp_seq == count - 1){
			double tvalSum;
			struct timeval tvalEnd; 
			double rttAverage;
			gettimeofday(&tvalEnd, NULL);
			tv_sub(&tvalEnd, &tvalBegin);
			tvalSum = tvalEnd.tv_sec * 1000.0 + tvalEnd.tv_usec / 1000.0; rttAverage = rttTotal / recvCount;
			puts("--- ping statistics ---");
			printf("%lld packets transmitted, %lld received, %.0lf%% packet loss, time %.2lfms\n", sendCount, recvCount, (sendCount - recvCount) * 100.0 / sendCount, tvalSum);
			printf("min rtt = %.3lf ms, avg rtt = %.3lf ms, max rtt = %.3lf ms\n", rttMin, rttAverage, rttMax);
			exit(0); 
		}

	} else if (verbose) {
		printf("  %d bytes from %s: type = %d, code = %d\n",
				icmplen, Sock_ntop_host(pr->sarecv, pr->salen),
				icmp->icmp_type, icmp->icmp_code);
	}
}

void
proc_v6(char *ptr, ssize_t len, struct timeval* tvrecv)
{
#ifdef	IPV6
	int					hlen1, icmp6len;
	double				rtt;
	struct ip6_hdr		*ip6;
	struct icmp6_hdr	*icmp6;
	struct timeval		*tvsend;

	ip6 = (struct ip6_hdr *) ptr;		/* start of IPv6 header */
	hlen1 = sizeof(struct ip6_hdr);
	if (ip6->ip6_nxt != IPPROTO_ICMPV6)
		err_quit("next header not IPPROTO_ICMPV6");

	icmp6 = (struct icmp6_hdr *) (ptr + hlen1);
	if ( (icmp6len = len - hlen1) < 8)
		err_quit("icmp6len (%d) < 8", icmp6len);

	if (icmp6->icmp6_type == ICMP6_ECHO_REPLY) {
		if (icmp6->icmp6_id != pid)
			return;			/* not a response to our ECHO_REQUEST */
		if (icmp6len < 16)
			err_quit("icmp6len (%d) < 16", icmp6len);

		tvsend = (struct timeval *) (icmp6 + 1);
		tv_sub(tvrecv, tvsend);
		rtt = tvrecv->tv_sec * 1000.0 + tvrecv->tv_usec / 1000.0;

		printf("%d bytes from %s: seq=%u, hlim=%d, rtt=%.3f ms\n",
				icmp6len, Sock_ntop_host(pr->sarecv, pr->salen),
				icmp6->icmp6_seq, ip6->ip6_hlim, rtt);

	} else if (verbose) {
		printf("  %d bytes from %s: type = %d, code = %d\n",
				icmp6len, Sock_ntop_host(pr->sarecv, pr->salen),
				icmp6->icmp6_type, icmp6->icmp6_code);
	}
#endif	/* IPV6 */
}

unsigned short
in_cksum(unsigned short *addr, int len)
{
        int                             nleft = len;
        int                             sum = 0;
        unsigned short  *w = addr;
        unsigned short  answer = 0;

        /*
         * Our algorithm is simple, using a 32 bit accumulator (sum), we add
         * sequential 16 bit words to it, and at the end, fold back all the
         * carry bits from the top 16 bits into the lower 16 bits.
         */
        while (nleft > 1)  {
                sum += *w++;
                nleft -= 2;
        }

                /* 4mop up an odd byte, if necessary */
        if (nleft == 1) {
                *(unsigned char *)(&answer) = *(unsigned char *)w ;
                sum += answer;
        }

                /* 4add back carry outs from top 16 bits to low 16 bits */
        sum = (sum >> 16) + (sum & 0xffff);     /* add hi 16 to low 16 */
        sum += (sum >> 16);                     /* add carry */
        answer = ~sum;                          /* truncate to 16 bits */
        return(answer);
}

void
send_v4(void)
{
	int			len;
	struct icmp	*icmp;

	icmp = (struct icmp *) sendbuf;
	icmp->icmp_type = ICMP_ECHO;
	icmp->icmp_code = 0;
	icmp->icmp_id = pid;
	icmp->icmp_seq = nsent++;
	gettimeofday((struct timeval *) icmp->icmp_data, NULL);

	len = 8 + datalen;		/* checksum ICMP header and data */
	icmp->icmp_cksum = 0;
	icmp->icmp_cksum = in_cksum((u_short *) icmp, len);

	sendto(sockfd, sendbuf, len, 0, pr->sasend, pr->salen);
}

void
send_v6()
{
#ifdef	IPV6
	int					len;
	struct icmp6_hdr	*icmp6;

	icmp6 = (struct icmp6_hdr *) sendbuf;
	icmp6->icmp6_type = ICMP6_ECHO_REQUEST;
	icmp6->icmp6_code = 0;
	icmp6->icmp6_id = pid;
	icmp6->icmp6_seq = nsent++;
	gettimeofday((struct timeval *) (icmp6 + 1), NULL);

	len = 8 + datalen;		/* 8-byte ICMPv6 header */

	sendto(sockfd, sendbuf, len, 0, pr->sasend, pr->salen);
		/* 4kernel calculates and stores checksum for us */
#endif	/* IPV6 */
}

void
readloop(void)
{
	int				size;
	char			recvbuf[BUFSIZE];
	socklen_t		len;
	ssize_t			n;
	struct timeval	tval;

	sockfd = socket(pr->sasend->sa_family, SOCK_RAW, pr->icmpproto);
	setuid(getuid());		/* don't need special permissions any more */

	if(broadcast_flag == 1){
		const int opt = -1;
		setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)); 
	}

	if(ttl_flag == 1){
		printf("set ttl = %d!\n", ttl);
		setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
	}

	size = 60 * 1024;		/* OK if setsockopt fails */
	setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	gettimeofday(&tvalBegin, NULL);
	sig_alrm(SIGALRM);		/* send first packet */

	for ( ; ; ) {
		len = pr->salen;
		n = recvfrom(sockfd, recvbuf, sizeof(recvbuf), 0, pr->sarecv, &len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			else
				err_sys("recvfrom error");
		}

		gettimeofday(&tval, NULL);
		(*pr->fproc)(recvbuf, n, &tval);
	}
}

void
sig_alrm(int signo)
{
        (*pr->fsend)();
		sendCount++;
        alarm(send_interval);
        return;         /* probably interrupts recvfrom() */
}

void
tv_sub(struct timeval *out, struct timeval *in)
{
	if ( (out->tv_usec -= in->tv_usec) < 0) {	/* out -= in */
		--out->tv_sec;
		out->tv_usec += 1000000;
	}
	out->tv_sec -= in->tv_sec;
}




char *
sock_ntop_host(const struct sockaddr *sa, socklen_t salen)
{
    static char str[128];               /* Unix domain is largest */

        switch (sa->sa_family) {
        case AF_INET: {
                struct sockaddr_in      *sin = (struct sockaddr_in *) sa;

                if (inet_ntop(AF_INET, &sin->sin_addr, str, sizeof(str)) == NULL)
                        return(NULL);
                return(str);
        }

#ifdef  IPV6
        case AF_INET6: {
                struct sockaddr_in6     *sin6 = (struct sockaddr_in6 *) sa;

                if (inet_ntop(AF_INET6, &sin6->sin6_addr, str, sizeof(str)) == NULL)
                        return(NULL);
                return(str);
        }
#endif

#ifdef  HAVE_SOCKADDR_DL_STRUCT
        case AF_LINK: {
                struct sockaddr_dl      *sdl = (struct sockaddr_dl *) sa;

                if (sdl->sdl_nlen > 0)
                        snprintf(str, sizeof(str), "%*s",
                                         sdl->sdl_nlen, &sdl->sdl_data[0]);
                else
                        snprintf(str, sizeof(str), "AF_LINK, index=%d", sdl->sdl_index);
                return(str);
        }
#endif
        default:
                snprintf(str, sizeof(str), "sock_ntop_host: unknown AF_xxx: %d, len %d",
                                 sa->sa_family, salen);
                return(str);
        }
    return (NULL);
}

char *
Sock_ntop_host(const struct sockaddr *sa, socklen_t salen)
{
        char    *ptr;

        if ( (ptr = sock_ntop_host(sa, salen)) == NULL)
                err_sys("sock_ntop_host error");        /* inet_ntop() sets errno */
        return(ptr);
}

struct addrinfo *
host_serv(const char *host, const char *serv, int family, int socktype)
{
        int                             n;
        struct addrinfo hints, *res;

        bzero(&hints, sizeof(struct addrinfo));
        hints.ai_flags = AI_CANONNAME;  /* always return canonical name */
        hints.ai_family = family;               /* AF_UNSPEC, AF_INET, AF_INET6, etc. */
        hints.ai_socktype = socktype;   /* 0, SOCK_STREAM, SOCK_DGRAM, etc. */

        if ( (n = getaddrinfo(host, serv, &hints, &res)) != 0)
                return(NULL);

        return(res);    /* return pointer to first on linked list */
}
/* end host_serv */

static void
err_doit(int errnoflag, int level, const char *fmt, va_list ap)
{
        int             errno_save, n;
        char    buf[MAXLINE];

        errno_save = errno;             /* value caller might want printed */
#ifdef  HAVE_VSNPRINTF
        vsnprintf(buf, sizeof(buf), fmt, ap);   /* this is safe */
#else
        vsprintf(buf, fmt, ap);                                 /* this is not safe */
#endif
        n = strlen(buf);
        if (errnoflag)
                snprintf(buf+n, sizeof(buf)-n, ": %s", strerror(errno_save));
        strcat(buf, "\n");

        if (daemon_proc) {
                syslog(level, buf);
        } else {
                fflush(stdout);         /* in case stdout and stderr are the same */
                fputs(buf, stderr);
                fflush(stderr);
        }
        return;
}


/* Fatal error unrelated to a system call.
 * Print a message and terminate. */

void
err_quit(const char *fmt, ...)
{
        va_list         ap;

        va_start(ap, fmt);
        err_doit(0, LOG_ERR, fmt, ap);
        va_end(ap);
        exit(1);
}

/* Fatal error related to a system call.
 * Print a message and terminate. */

void
err_sys(const char *fmt, ...)
{
        va_list         ap;

        va_start(ap, fmt);
        err_doit(1, LOG_ERR, fmt, ap);
        va_end(ap);
        exit(1);
}

void help(){
	printf("-h	显示帮助信息 ./ping -h\n");
	printf("-b	允许ping一个广播地址，只用于IPv4 sudo ./ping -b 192.168.88.255\n");
	printf("-t  设置ttl值，只用于IPv4 sudo ./ping -t 10 baidu.com\n");
	printf("-q	安静模式。不显示每个收到的包的分析结果，只在结束时，显示汇总结果 sudo ./ping -q baidu.com\n");
    printf("-c  发送指定个数的数据包 sudo ./ping -c 10 baidu.com\n");
    printf("-s  发送指定大小的数据包 sudo ./ping -s 60 baidu.com\n");
    printf("-i  设置发送包的时间间隔 sudo ./ping -i 5 baidu.com\n");
    printf("-n  设置包的初始序列号 sudo ./ping -n 1 baidu.com\n");
	printf("-d  打印时间戳 sudo ./ping -d baidu.com\n");
	printf("-v  显示详细输出 sudo ./ping -v baidu.com\n");
}
