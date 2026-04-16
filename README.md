# [301mail](https://301333.xyz)-tgbot

定期拉取最新的邮件并通过 Telegram Bot 推送

## 依赖库

```bash
apk add curl-dev cjson-dev gumbo-parser-dev
```

## 编译

```bash
gcc -o 301mail_tgbot main.c \
    -lcurl -lcjson -lgumbo \
    -DACCOUNT_ID=<Your Cloud Mail accountId> \
    -DAUTH_TOKEN='"<Your Cloud Mail Authorization Request Header>"' \
    -DTG_URL='"https://api.telegram.org/bot<YOUR_BOT_TOKEN>/sendMessage"' \
    -DTG_CHAT_ID='"<Your Telegram Account ID>"'
```

## 用法

将编译好的程序扔到服务器的 `/etc/periodic/15min` 目录，让cron隔一段时间自动运行一次即可
